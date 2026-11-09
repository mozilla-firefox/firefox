
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BufferSurface_h_
#define BufferSurface_h_

#include <stdint.h>

#include "GLTypes.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsISupportsImpl.h"
#include "nsString.h"

typedef void* EGLImageKHR;
struct wl_buffer;

// The files bellow has exact description of all formats:
// media/ffvpx/libavutil/pixdesc.h
// media/ffvpx/libavutil/pixdesc.c

#ifndef VA_FOURCC_NV12
#  define VA_FOURCC_NV12 0x3231564E
#endif
#ifndef VA_FOURCC_I420
#  define VA_FOURCC_I420 0x30323449
#endif
#ifndef VA_FOURCC_YV12
#  define VA_FOURCC_YV12 0x32315659
#endif
#ifndef VA_FOURCC_P010
#  define VA_FOURCC_P010 0x30313050
#endif
#ifndef VA_FOURCC_P016
#  define VA_FOURCC_P016 0x36313050
#endif

#define BUFFER_SURFACE_PLANES 4

namespace mozilla {
namespace gfx {
class DrawTarget;
class FileHandleWrapper;
class DataSourceSurface;
}  // namespace gfx
namespace gl {
class GLContext;
}  // namespace gl
}  // namespace mozilla

class DMABufSurface;
class DMABufSurfaceRGBA;
class DMABufSurfaceYUV;
class SHMBufSurface;

class BufferSurface {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(BufferSurface)

  enum SurfaceType {
    SURFACE_RGBA = 0,
    SURFACE_YUV = 1,
  };

  nsAutoCString GetDebugTag() const;

#ifdef MOZ_LOGGING
  constexpr static const char* sSurfaceTypeNames[] = {"RGBA", "YUV"};
#endif

  virtual DMABufSurface* GetAsDMABufSurface() { return nullptr; }
  virtual DMABufSurfaceRGBA* GetAsDMABufSurfaceRGBA() { return nullptr; }
  virtual DMABufSurfaceYUV* GetAsDMABufSurfaceYUV() { return nullptr; }
  virtual SHMBufSurface* GetAsSHMBufSurface() { return nullptr; }

  // WidthAligned/HeightAligned is size of buffer while
  // Width/Height is size of actual content.
  int GetWidth(int aPlane = 0) const { return mWidth[aPlane]; }
  int GetHeight(int aPlane = 0) const { return mHeight[aPlane]; }
  mozilla::gfx::IntSize GetSize(uint8_t aPlane = 0) const {
    return mozilla::gfx::IntSize(GetWidth(aPlane), GetHeight(aPlane));
  }

  // CPU (shared memory) access to the surface. Default implementations are
  // no-op for surfaces which don't provide CPU-accessible memory (like DMABuf).
  virtual already_AddRefed<mozilla::gfx::DrawTarget> Lock() { return nullptr; }
  virtual void* GetImageData() { return nullptr; }
  // Distance in bytes between rows of the plane.
  int32_t GetStride(int aPlane = 0) const { return mStrides[aPlane]; }

  virtual bool CreateTextures(mozilla::gl::GLContext* aGLContext);
  // Surface memory may be recycled and hold a new frame while textures
  // created over it are still the old ones. Call it whenever surface content
  // changes to upload it again on next CreateTextures(). All planes are
  // refreshed at once to keep them in sync.
  void MarkTextureDirty() { mTextureIsDirty = true; }
  // Release all textures/EGLImages created over this surface
  // and the GL context they're tied to.
  void ReleaseTextures();
  // Returns true if any texture or EGLImage is created over this surface.
  bool HoldsTexture() const;
  GLuint GetTexture(int aPlane = 0) { return mTexture[aPlane]; }
  EGLImageKHR GetEGLImage(int aPlane = 0) { return mEGLImage[aPlane]; }
  // Number of textures created over this surface by CreateTextures().
  // It matches the number of buffer planes unless the whole surface
  // is sampled as a single texture.
  virtual int GetTextureCount() { return mBufferPlaneCount; }

  SurfaceType GetSurfaceType() const;
  const char* GetSurfaceTypeName() const {
    return sSurfaceTypeNames[static_cast<int>(GetSurfaceType())];
  };

  void SetFormat(mozilla::gfx::SurfaceFormat aFormat);

  bool HasAlpha() const;
  mozilla::gfx::SurfaceFormat GetFormat() const;
  int32_t GetFOURCCFormat() const { return mFOURCCFormat; };
  int GetFormatBPP() const;
  mozilla::gfx::ColorDepth GetColorDepth() const;

#ifdef MOZ_WAYLAND
  int GetWLFormat() const;
#endif

  virtual already_AddRefed<mozilla::gfx::DataSourceSurface>
  GetAsSourceSurface() = 0;

#ifdef MOZ_LOGGING
  void DumpToFile(const char* aFileName);
#endif

  void SetYUVColorSpace(mozilla::gfx::YUVColorSpace aColorSpace) {
    mColorSpace = aColorSpace;
  }
  mozilla::gfx::YUVColorSpace GetYUVColorSpace() { return mColorSpace; }
  void SetColorPrimaries(mozilla::gfx::ColorSpace2 aColorPrimaries) {
    mColorPrimaries = aColorPrimaries;
  }
  void SetTransferFunction(mozilla::gfx::TransferFunction aTransferFunction) {
    mTransferFunction = aTransferFunction;
  }
  mozilla::gfx::TransferFunction GetTransferFunction() {
    return mTransferFunction;
  }
  bool IsHDRSurface() {
    return mTransferFunction == mozilla::gfx::TransferFunction::PQ ||
           mTransferFunction == mozilla::gfx::TransferFunction::HLG;
  }

  void SetHDRMetadata(mozilla::gfx::HDRMetadata aHDRMetadata) {
    mHDRMetadata = aHDRMetadata;
  }

  mozilla::gfx::HDRMetadata GetHDRMetadata() { return mHDRMetadata; }

  bool IsFullRange() { return mColorRange == mozilla::gfx::ColorRange::FULL; };
  void SetColorRange(mozilla::gfx::ColorRange aColorRange) {
    mColorRange = aColorRange;
  };

  void SetWPChromaLocation(uint32_t aWPChromaLocation) {
    mWPChromaLocation = aWPChromaLocation;
  }
  uint32_t GetWPChromaLocation() { return mWPChromaLocation; }

#ifdef MOZ_WAYLAND
  int GetWLColorCoeficients();
#  ifdef MOZ_LOGGING
  static const char* GetWLColorCoeficientsName(int aWLColorCoeficients);
#  endif
#endif

#ifdef MOZ_WAYLAND
  // Create wl_buffer over BufferSurface, ownership is transfered to caller.
  // If underlying BufferSurface is deleted before wl_buffer destroy,
  // behaviour is undefined and may lead to rendering artifacts as
  // GPU memory may be reused.
  //
  // Every CreateWlBuffer() creates new wl_buffer and one BufferSurface
  // can have multiple wl_buffers created over it.
  // That's correct as one BufferSurfacemay be attached and rendred by
  // more wl_surfaces at the same time.
  virtual wl_buffer* CreateWlBuffer() { return nullptr; }
#endif

  // Set and get a global surface UID. The UID is shared across process
  // and it's used to track surface lifetime in various parts of rendering
  // engine.
  virtual uint32_t GetUID() const { return mUID; };

  // Get PID of process where surface was created. PID+UID gives global
  // surface ID which is unique for all used surfaces.
  uint32_t GetPID() const { return mPID; };

  bool Matches(BufferSurface* aSurface) const {
    return mUID == aSurface->mUID && mPID == aSurface->mPID;
  }

  bool CanRecycle() const { return mCanRecycle && mPID; }
  void DisableRecycle() { mCanRecycle = false; }

 protected:
  BufferSurface() = default;
  virtual ~BufferSurface();

  // Create texture over aPlane, it's called by CreateTextures() only.
  // Returns true and does nothing if the plane texture already exists.
  virtual bool CreateTexture(mozilla::gl::GLContext* aGLContext, int aPlane) {
    return false;
  }

  // Set sampler state of aTexture to the one all surface textures are
  // expected to use. Freshly generated textures default to a mipmap based
  // filter which is incomplete without mipmaps, so it has to be called for
  // every texture we create. Texture binding of aTarget is restored.
  // aTarget defaults to LOCAL_GL_TEXTURE_2D.
  static void SetTextureFilters(mozilla::gl::GLContext* aGL, GLuint aTexture,
                                GLenum aTarget);
  static void SetTextureFilters(mozilla::gl::GLContext* aGL, GLuint aTexture);

  size_t GetUsedMemory(int aWidth, int aHeight) const;

  // Actual FOURCC format of whole surface (includes all planes).
  // Some formats can't be described by fourcc only (YUV420P10 for instance)
  // so we use additional ColorDepth.
  int32_t mFOURCCFormat = 0;
  mozilla::Maybe<mozilla::gfx::ColorDepth> mColorDepth;

  // Size of the actual content of each plane. Single plane surfaces
  // use the first entry only.
  int mWidth[BUFFER_SURFACE_PLANES] = {};
  int mHeight[BUFFER_SURFACE_PLANES] = {};

  // Configuration of surface planes, it depends on surface modifiers.
  // RGBA surface may use one RGBA plane or two planes (RGB + A)
  // YUV surfaces use various planes setup (Y + UV planes or Y+U+V planes)
  int mBufferPlaneCount = 0;
  int32_t mStrides[BUFFER_SURFACE_PLANES] = {};
  int32_t mOffsets[BUFFER_SURFACE_PLANES] = {};

  // mGL is tied to textures/eglimages created over surface and it's null for
  // surface without textures/eglimages.
  RefPtr<mozilla::gl::GLContext> mGL;

  // Textures and EGLImages created over surface planes by CreateTextures().
  // They're owned by mGL context and released by ReleaseTextures().
  EGLImageKHR mEGLImage[BUFFER_SURFACE_PLANES] = {};
  GLuint mTexture[BUFFER_SURFACE_PLANES] = {};

  // Set when underlying surface memory was changed.
  bool mTextureIsDirty = false;

  mozilla::gfx::ColorRange mColorRange = mozilla::gfx::ColorRange::FULL;
  mozilla::gfx::YUVColorSpace mColorSpace =
      mozilla::gfx::YUVColorSpace::Default;
  mozilla::gfx::ColorSpace2 mColorPrimaries =
      mozilla::gfx::ColorSpace2::UNKNOWN;
  mozilla::gfx::TransferFunction mTransferFunction =
      mozilla::gfx::TransferFunction::Default;
  mozilla::gfx::HDRMetadata mHDRMetadata{};
  // Chroma location in wp_color_representation_surface_v1_chroma_location
  // format.
  uint32_t mWPChromaLocation = 0;

  // mUID/mPID is set when surface is created and/or exported to different
  // process. Allows to identify surfaces created by different process.
  // Used by DMABuf surfaces only.
  uint32_t mUID = 0;
  uint32_t mPID = 0;

  // Internal surface flag, it's not exported (Serialized).
  // If set to false we can't recycle this surfaces as we can't ensure
  // mUID/mPID consistency. Also mPID may be zero in this case.
  // Applies to copied DMABuf surfaces for instance.
  bool mCanRecycle = true;
};

#endif
