/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/ImageBridgeChild.h"  // for ImageBridgeChild
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/RefPtr.h"
#include "gfx2DGlue.h"
#include "gfxImageSurface.h"
#include "gfxPlatform.h"
#include "gfxTypes.h"
#include "ImageContainer.h"
#include "mozilla/gtest/MozHelpers.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/ipc/Shmem.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;

/*
 * This test performs the following actions:
 * - creates a surface
 * - initialize a texture client with it
 * - serilaizes the texture client
 * - deserializes the data into a texture host
 * - reads the surface from the texture host.
 *
 * The surface in the end should be equal to the inital one.
 * This test is run for different combinations of texture types and
 * image formats.
 */

namespace mozilla {
namespace layers {

class TestSurfaceAllocator final : public ISurfaceAllocator {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TestSurfaceAllocator, final);

  TestSurfaceAllocator() = default;

  bool IsSameProcess() const override { return true; }

 private:
  virtual ~TestSurfaceAllocator() = default;
};

// fills the surface with values betwee 0 and 100.
static void SetupSurface(gfxImageSurface* surface) {
  int bpp = gfxASurface::BytePerPixelFromFormat(surface->Format());
  int stride = surface->Stride();
  uint8_t val = 0;
  uint8_t* data = surface->Data();
  for (int y = 0; y < surface->Height(); ++y) {
    for (int x = 0; x < surface->Height(); ++x) {
      for (int b = 0; b < bpp; ++b) {
        data[y * stride + x * bpp + b] = val;
        if (val == 100) {
          val = 0;
        } else {
          ++val;
        }
      }
    }
  }
}

// return true if two surfaces contain the same data
static void AssertSurfacesEqual(gfxImageSurface* surface1,
                                gfxImageSurface* surface2) {
  ASSERT_EQ(surface1->GetSize(), surface2->GetSize());
  ASSERT_EQ(surface1->Format(), surface2->Format());

  uint8_t* data1 = surface1->Data();
  uint8_t* data2 = surface2->Data();
  int stride1 = surface1->Stride();
  int stride2 = surface2->Stride();
  int bpp = gfxASurface::BytePerPixelFromFormat(surface1->Format());

  for (int y = 0; y < surface1->Height(); ++y) {
    for (int x = 0; x < surface1->Width(); ++x) {
      for (int b = 0; b < bpp; ++b) {
        ASSERT_EQ(data1[y * stride1 + x * bpp + b],
                  data2[y * stride2 + x * bpp + b]);
      }
    }
  }
}

static void AssertSurfacesEqual(SourceSurface* surface1,
                                SourceSurface* surface2) {
  ASSERT_EQ(surface1->GetSize(), surface2->GetSize());
  ASSERT_EQ(surface1->GetFormat(), surface2->GetFormat());

  RefPtr<DataSourceSurface> dataSurface1 = surface1->GetDataSurface();
  RefPtr<DataSourceSurface> dataSurface2 = surface2->GetDataSurface();
  DataSourceSurface::MappedSurface map1;
  DataSourceSurface::MappedSurface map2;
  if (!dataSurface1->Map(DataSourceSurface::READ, &map1)) {
    return;
  }
  if (!dataSurface2->Map(DataSourceSurface::READ, &map2)) {
    dataSurface1->Unmap();
    return;
  }
  uint8_t* data1 = map1.mData;
  uint8_t* data2 = map2.mData;
  int stride1 = map1.mStride;
  int stride2 = map2.mStride;
  int bpp = BytesPerPixel(surface1->GetFormat());
  int width = surface1->GetSize().width;
  int height = surface1->GetSize().height;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      for (int b = 0; b < bpp; ++b) {
        ASSERT_EQ(data1[y * stride1 + x * bpp + b],
                  data2[y * stride2 + x * bpp + b]);
      }
    }
  }

  dataSurface1->Unmap();
  dataSurface2->Unmap();
}

// Run the test for a texture client and a surface
void TestTextureClientSurface(TextureClient* texture,
                              gfxImageSurface* surface) {
  // client allocation
  ASSERT_TRUE(texture->CanExposeDrawTarget());

  ASSERT_TRUE(texture->Lock(OpenMode::OPEN_READ_WRITE));
  // client painting
  RefPtr<DrawTarget> dt = texture->BorrowDrawTarget();
  RefPtr<SourceSurface> source =
      gfxPlatform::GetPlatform()->GetSourceSurfaceForSurface(dt, surface);
  dt->CopySurface(source, IntRect(IntPoint(), source->GetSize()), IntPoint());

  RefPtr<SourceSurface> snapshot = dt->Snapshot();

  AssertSurfacesEqual(snapshot, source);

  dt = nullptr;  // drop reference before calling Unlock()
  texture->Unlock();

  // client serialization
  SurfaceDescriptor descriptor;
  ASSERT_TRUE(texture->ToSurfaceDescriptor(descriptor));

  ASSERT_NE(descriptor.type(), SurfaceDescriptor::Tnull_t);

  // host deserialization
  RefPtr<TestSurfaceAllocator> deallocator = new TestSurfaceAllocator();
  RefPtr<TextureHost> host = CreateBackendIndependentTextureHost(
      descriptor, deallocator, LayersBackend::LAYERS_NONE, texture->GetFlags());

  ASSERT_TRUE(host.get() != nullptr);
  ASSERT_EQ(host->GetFlags(), texture->GetFlags());
}

// Same as above, for YCbCr surfaces
void TestTextureClientYCbCr(TextureClient* client, PlanarYCbCrData& ycbcrData) {
  client->Lock(OpenMode::OPEN_READ_WRITE);
  UpdateYCbCrTextureClient(client, ycbcrData);
  client->Unlock();

  // client serialization
  SurfaceDescriptor descriptor;
  ASSERT_TRUE(client->ToSurfaceDescriptor(descriptor));

  ASSERT_EQ(descriptor.type(), SurfaceDescriptor::TSurfaceDescriptorBuffer);
  auto bufferDesc = descriptor.get_SurfaceDescriptorBuffer();
  ASSERT_EQ(bufferDesc.desc().type(), BufferDescriptor::TYCbCrDescriptor);
  auto ycbcrDesc = bufferDesc.desc().get_YCbCrDescriptor();
  ASSERT_EQ(ycbcrDesc.ySize(), ycbcrData.YDataSize());
  ASSERT_EQ(ycbcrDesc.cbCrSize(), ycbcrData.CbCrDataSize());
  ASSERT_EQ(ycbcrDesc.stereoMode(), ycbcrData.mStereoMode);

  // host deserialization
  RefPtr<TestSurfaceAllocator> deallocator = new TestSurfaceAllocator();
  RefPtr<TextureHost> textureHost = CreateBackendIndependentTextureHost(
      descriptor, deallocator, LayersBackend::LAYERS_NONE, client->GetFlags());

  RefPtr<BufferTextureHost> host =
      static_cast<BufferTextureHost*>(textureHost.get());

  ASSERT_TRUE(host.get() != nullptr);
  ASSERT_EQ(host->GetFlags(), client->GetFlags());
}

static gfx::HDRMetadata MakeTestHDRMetadata() {
  gfx::Smpte2086Metadata smpte2086;
  smpte2086.displayPrimaryRed = {0.708f, 0.292f};
  smpte2086.displayPrimaryGreen = {0.170f, 0.797f};
  smpte2086.displayPrimaryBlue = {0.131f, 0.046f};
  smpte2086.whitePoint = {0.3127f, 0.3290f};
  smpte2086.maxLuminance = 1000.0f;
  smpte2086.minLuminance = 0.001f;
  gfx::HDRMetadata hdrMetadata;
  hdrMetadata.mSmpte2086 = Some(smpte2086);
  hdrMetadata.mContentLightLevel = Some(gfx::ContentLightLevel{1000, 400});
  return hdrMetadata;
}

// Helper: creates a YCbCr texture with the given transfer function and
// optional HDR metadata, then verifies they are correctly written into the
// YCbCrDescriptor by CreateForYCbCr.
void TestYCbCrDescriptorTransferFunction(
    TransferFunction aTF, Maybe<gfx::HDRMetadata> aHDRMetadata,
    RefPtr<ImageBridgeChild> aImageBridge) {
  RefPtr<gfxImageSurface> ySurface =
      new gfxImageSurface(IntSize(400, 300), SurfaceFormat::A8);
  RefPtr<gfxImageSurface> cbSurface =
      new gfxImageSurface(IntSize(200, 150), SurfaceFormat::A8);
  RefPtr<gfxImageSurface> crSurface =
      new gfxImageSurface(IntSize(200, 150), SurfaceFormat::A8);
  SetupSurface(ySurface.get());
  SetupSurface(cbSurface.get());
  SetupSurface(crSurface.get());

  PlanarYCbCrData clientData;
  clientData.mYChannel = ySurface->Data();
  clientData.mCbChannel = cbSurface->Data();
  clientData.mCrChannel = crSurface->Data();
  clientData.mPictureRect = IntRect(IntPoint(0, 0), ySurface->GetSize());
  clientData.mYStride = ySurface->Stride();
  clientData.mCbCrStride = cbSurface->Stride();
  clientData.mStereoMode = StereoMode::MONO;
  clientData.mYUVColorSpace = YUVColorSpace::BT2020;
  clientData.mColorDepth = ColorDepth::COLOR_8;
  clientData.mTransferFunction = aTF;
  clientData.mHDRMetadata = aHDRMetadata;
  clientData.mChromaSubsampling = ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  clientData.mYSkip = 0;
  clientData.mCbSkip = 0;
  clientData.mCrSkip = 0;

  RefPtr<TextureClient> client = TextureClient::CreateForYCbCr(
      aImageBridge, clientData.mPictureRect, clientData.YDataSize(),
      clientData.mYStride, clientData.CbCrDataSize(), clientData.mCbCrStride,
      StereoMode::MONO, ColorDepth::COLOR_8, YUVColorSpace::BT2020,
      ColorRange::LIMITED, aTF, clientData.mChromaSubsampling,
      TextureFlags::DEALLOCATE_CLIENT, aHDRMetadata);

  TestTextureClientYCbCr(client, clientData);

  SurfaceDescriptor descriptor;
  ASSERT_TRUE(client->ToSurfaceDescriptor(descriptor));
  auto ycbcrDesc =
      descriptor.get_SurfaceDescriptorBuffer().desc().get_YCbCrDescriptor();
  ASSERT_EQ(ycbcrDesc.transferFunction(), aTF);
  if (aHDRMetadata.isNothing()) {
    ASSERT_TRUE(ycbcrDesc.hdrMetadata().isNothing());
    return;
  }
  ASSERT_TRUE(ycbcrDesc.hdrMetadata().isSome());
  ASSERT_TRUE(ycbcrDesc.hdrMetadata()->mSmpte2086.isSome());
  EXPECT_FLOAT_EQ(ycbcrDesc.hdrMetadata()->mSmpte2086->maxLuminance, 1000.0f);
  EXPECT_FLOAT_EQ(ycbcrDesc.hdrMetadata()->mSmpte2086->minLuminance, 0.001f);
  EXPECT_FLOAT_EQ(ycbcrDesc.hdrMetadata()->mSmpte2086->displayPrimaryRed.x,
                  0.708f);
  EXPECT_FLOAT_EQ(ycbcrDesc.hdrMetadata()->mSmpte2086->whitePoint.y, 0.3290f);
  ASSERT_TRUE(ycbcrDesc.hdrMetadata()->mContentLightLevel.isSome());
  ASSERT_EQ(ycbcrDesc.hdrMetadata()->mContentLightLevel->maxContentLightLevel,
            1000);
  ASSERT_EQ(
      ycbcrDesc.hdrMetadata()->mContentLightLevel->maxFrameAverageLightLevel,
      400);
}

}  // namespace layers
}  // namespace mozilla

TEST(Layers, TextureSerialization)
{
  // the test is run on all the following image formats
  gfxImageFormat formats[3] = {
      SurfaceFormat::A8R8G8B8_UINT32,
      SurfaceFormat::X8R8G8B8_UINT32,
      SurfaceFormat::A8,
  };

  for (int f = 0; f < 3; ++f) {
    RefPtr<gfxImageSurface> surface =
        new gfxImageSurface(IntSize(400, 300), formats[f]);
    SetupSurface(surface.get());
    AssertSurfacesEqual(surface, surface);

    auto texData = BufferTextureData::Create(
        surface->GetSize(), gfx::ImageFormatToSurfaceFormat(surface->Format()),
        gfx::ColorSpace2::SRGB, gfx::TransferFunction::SRGB,
        gfx::BackendType::CAIRO, LayersBackend::LAYERS_NONE,
        TextureFlags::DEALLOCATE_CLIENT, ALLOC_DEFAULT, nullptr);
    ASSERT_TRUE(!!texData);

    RefPtr<TextureClient> client =
        new TextureClient(texData, TextureFlags::DEALLOCATE_CLIENT, nullptr);

    TestTextureClientSurface(client, surface);

    // XXX - Test more texture client types.
  }
}

TEST(Layers, TextureYCbCrSerialization)
{
  RefPtr<gfxImageSurface> ySurface =
      new gfxImageSurface(IntSize(400, 300), SurfaceFormat::A8);
  RefPtr<gfxImageSurface> cbSurface =
      new gfxImageSurface(IntSize(200, 150), SurfaceFormat::A8);
  RefPtr<gfxImageSurface> crSurface =
      new gfxImageSurface(IntSize(200, 150), SurfaceFormat::A8);
  SetupSurface(ySurface.get());
  SetupSurface(cbSurface.get());
  SetupSurface(crSurface.get());

  PlanarYCbCrData clientData;
  clientData.mYChannel = ySurface->Data();
  clientData.mCbChannel = cbSurface->Data();
  clientData.mCrChannel = crSurface->Data();
  clientData.mPictureRect = IntRect(IntPoint(0, 0), ySurface->GetSize());
  clientData.mYStride = ySurface->Stride();
  clientData.mCbCrStride = cbSurface->Stride();
  clientData.mStereoMode = StereoMode::MONO;
  clientData.mYUVColorSpace = YUVColorSpace::BT601;
  clientData.mColorDepth = ColorDepth::COLOR_8;
  clientData.mTransferFunction = TransferFunction::BT709;
  clientData.mChromaSubsampling = ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  clientData.mYSkip = 0;
  clientData.mCbSkip = 0;
  clientData.mCrSkip = 0;
  clientData.mCrSkip = 0;

  uint32_t namespaceId = 1;
  ImageBridgeChild::InitSameProcess(namespaceId);

  RefPtr<ImageBridgeChild> imageBridge = ImageBridgeChild::GetSingleton();
  static int retry = 5;
  while (!imageBridge->IPCOpen() && retry) {
    // IPDL connection takes time especially in slow testing environment, like
    // VM machines. Here we added retry mechanism to wait for IPDL connnection.
#ifdef XP_WIN
    Sleep(1);
#else
    sleep(1);
#endif
    retry--;
  }

  // Skip this testing if IPDL connection is not ready
  if (!retry && !imageBridge->IPCOpen()) {
    return;
  }

  RefPtr<TextureClient> client = TextureClient::CreateForYCbCr(
      imageBridge, clientData.mPictureRect, clientData.YDataSize(),
      clientData.mYStride, clientData.CbCrDataSize(), clientData.mCbCrStride,
      StereoMode::MONO, ColorDepth::COLOR_8, YUVColorSpace::BT601,
      ColorRange::LIMITED, TransferFunction::BT709,
      clientData.mChromaSubsampling, TextureFlags::DEALLOCATE_CLIENT);

  TestTextureClientYCbCr(client, clientData);

  // XXX - Test more texture client types.
}

TEST(Layers, TextureYCbCrTransferFunctionPQ)
{
  RefPtr<ImageBridgeChild> imageBridge = ImageBridgeChild::GetSingleton();
  if (!imageBridge || !imageBridge->IPCOpen()) {
    return;
  }
  TestYCbCrDescriptorTransferFunction(TransferFunction::PQ,
                                      Some(MakeTestHDRMetadata()), imageBridge);
}

TEST(Layers, TextureYCbCrTransferFunctionHLG)
{
  RefPtr<ImageBridgeChild> imageBridge = ImageBridgeChild::GetSingleton();
  if (!imageBridge || !imageBridge->IPCOpen()) {
    return;
  }
  TestYCbCrDescriptorTransferFunction(TransferFunction::HLG,
                                      Some(MakeTestHDRMetadata()), imageBridge);
}

TEST(Layers, TextureYCbCrNoHDRMetadata)
{
  RefPtr<ImageBridgeChild> imageBridge = ImageBridgeChild::GetSingleton();
  if (!imageBridge || !imageBridge->IPCOpen()) {
    return;
  }
  TestYCbCrDescriptorTransferFunction(TransferFunction::BT709, Nothing(),
                                      imageBridge);
}

TEST(Layers, TextureYCbCrRejectsDisplayRectExceedingYSize)
{
  // The gtest harness sets XPCOM_DEBUG_BREAK=stack-and-abort, so the
  // NS_ERROR in the rejection path of CreateBackendIndependentTextureHost
  // becomes a MOZ_CRASH_UNSAFE in DEBUG builds. We split the verification:
  // DEBUG builds observe the death; opt builds (where NS_ERROR is a no-op)
  // check that the function returns nullptr directly.
  auto shmemBuilder = ipc::Shmem::Builder(128);
  ASSERT_TRUE(shmemBuilder);
  auto [msg, shmem] = shmemBuilder.Build(100, false, 0);
  ASSERT_TRUE(shmem.IsWritable());

  // [[maybe_unused]]: on Darwin, EXPECT_DEATH_WRAP is a no-op (death tests
  // are too slow there), so in DEBUG-on-Darwin builds runRejection is
  // defined but never called. Suppress the resulting -Wunused-variable
  // -Werror without an #ifdef.
  [[maybe_unused]] auto runRejection = [&]() -> RefPtr<TextureHost> {
    // display (8x8) exceeds ySize (4x4).
    // Offsets: yOffset=0, cbOffset=16, crOffset=20
    YCbCrDescriptor ycbcrDesc(
        IntRect(0, 0, 8, 8), IntSize(4, 4), 4u, IntSize(2, 2), 2u, 0u, 16u, 20u,
        StereoMode::MONO, ColorDepth::COLOR_8, YUVColorSpace::BT601,
        ColorRange::LIMITED, TransferFunction::BT709,
        ChromaSubsampling::HALF_WIDTH_AND_HEIGHT, mozilla::Nothing());

    BufferDescriptor bufDesc(ycbcrDesc);
    MemoryOrShmem memOrShmem(shmem);
    SurfaceDescriptorBuffer sdBuffer(bufDesc, memOrShmem);
    SurfaceDescriptor sd(sdBuffer);

    RefPtr<TestSurfaceAllocator> deallocator = new TestSurfaceAllocator();
    return CreateBackendIndependentTextureHost(sd, deallocator,
                                               LayersBackend::LAYERS_NONE,
                                               TextureFlags::DEALLOCATE_CLIENT);
  };

#ifdef DEBUG
  SAVE_GDB_SLEEP_LOCAL();
  EXPECT_DEATH_WRAP({ (void)runRejection(); }, "");
  RESTORE_GDB_SLEEP_LOCAL();
#else
  RefPtr<TextureHost> host = runRejection();
  EXPECT_EQ(host.get(), nullptr)
      << "Must reject YCbCr descriptors where display rect exceeds Y plane "
         "dimensions (display=8x8 vs ySize=4x4).";
#endif
}

TEST(Layers, TextureYCbCrRejectsCbCrSizeInconsistentWithDisplay)
{
  auto shmemBuilder = ipc::Shmem::Builder(128);
  ASSERT_TRUE(shmemBuilder);
  auto [msg, shmem] = shmemBuilder.Build(101, false, 0);
  ASSERT_TRUE(shmem.IsWritable());

  // [[maybe_unused]]: on Darwin, EXPECT_DEATH_WRAP is a no-op (death tests
  // are too slow there), so in DEBUG-on-Darwin builds runRejection is
  // defined but never called. Suppress the resulting -Wunused-variable
  // -Werror without an #ifdef.
  [[maybe_unused]] auto runRejection = [&]() -> RefPtr<TextureHost> {
    // display (8x8) fits within ySize (8x8), but cbCrSize (1x1) is too
    // small for the chroma dimensions derived from display.
    // Offsets: yOffset=0, cbOffset=64, crOffset=68
    YCbCrDescriptor ycbcrDesc(
        IntRect(0, 0, 8, 8), IntSize(8, 8), 8u, IntSize(1, 1), 1u, 0u, 64u, 68u,
        StereoMode::MONO, ColorDepth::COLOR_8, YUVColorSpace::BT601,
        ColorRange::LIMITED, TransferFunction::BT709,
        ChromaSubsampling::HALF_WIDTH_AND_HEIGHT, mozilla::Nothing());

    BufferDescriptor bufDesc(ycbcrDesc);
    MemoryOrShmem memOrShmem(shmem);
    SurfaceDescriptorBuffer sdBuffer(bufDesc, memOrShmem);
    SurfaceDescriptor sd(sdBuffer);

    RefPtr<TestSurfaceAllocator> deallocator = new TestSurfaceAllocator();
    return CreateBackendIndependentTextureHost(sd, deallocator,
                                               LayersBackend::LAYERS_NONE,
                                               TextureFlags::DEALLOCATE_CLIENT);
  };

#ifdef DEBUG
  SAVE_GDB_SLEEP_LOCAL();
  EXPECT_DEATH_WRAP({ (void)runRejection(); }, "");
  RESTORE_GDB_SLEEP_LOCAL();
#else
  RefPtr<TextureHost> host = runRejection();
  EXPECT_EQ(host.get(), nullptr)
      << "Must reject YCbCr descriptors where CbCr plane size is too small "
         "for the chroma dimensions derived from display rect "
         "(ChromaSize(8x8)=4x4 vs cbCrSize=1x1).";
#endif
}
