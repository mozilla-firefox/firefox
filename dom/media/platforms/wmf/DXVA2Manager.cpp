/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_AV1
#  include "AOMDecoder.h"
#endif
#include "DXVA2Manager.h"
#include <d3d11.h>
#include "DriverCrashGuard.h"
#include "GfxDriverInfo.h"
#include "ImageContainer.h"
#include "MFTDecoder.h"
#include "MediaTelemetryConstants.h"
#include "PerformanceRecorder.h"
#include "VideoUtils.h"
#include "VPXDecoder.h"
#include "WMFUtils.h"
#include "gfxCrashReporterUtils.h"
#include "gfxWindowsPlatform.h"
#include "mfapi.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/glean/DomMediaPlatformsWmfMetrics.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/layers/CompositeProcessD3D11FencesHolderMap.h"
#include "mozilla/layers/D3D11ShareHandleImage.h"
#include "mozilla/layers/D3D11ZeroCopyTextureImage.h"
#include "mozilla/layers/FenceD3D11.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/TextureD3D11.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/layers/VideoProcessorD3D11.h"
#include "mozilla/mscom/EnsureMTA.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"

const GUID MF_XVP_PLAYBACK_MODE = {
    0x3c5d293f,
    0xad67,
    0x4e29,
    {0xaf, 0x12, 0xcf, 0x3e, 0x23, 0x8a, 0xcc, 0xe9}};

DEFINE_GUID(MF_LOW_LATENCY, 0x9c27891a, 0xed7a, 0x40e1, 0x88, 0xe8, 0xb2, 0x27,
            0x27, 0xa0, 0x24, 0xee);

// R600, R700, Evergreen and Cayman AMD cards. These support DXVA via UVD3 or
// earlier, and don't handle 1080p60 well.
static const DWORD sAMDPreUVD4[] = {
    // clang-format off
  0x9400, 0x9401, 0x9402, 0x9403, 0x9405, 0x940a, 0x940b, 0x940f, 0x94c0, 0x94c1, 0x94c3, 0x94c4, 0x94c5,
  0x94c6, 0x94c7, 0x94c8, 0x94c9, 0x94cb, 0x94cc, 0x94cd, 0x9580, 0x9581, 0x9583, 0x9586, 0x9587, 0x9588,
  0x9589, 0x958a, 0x958b, 0x958c, 0x958d, 0x958e, 0x958f, 0x9500, 0x9501, 0x9504, 0x9505, 0x9506, 0x9507,
  0x9508, 0x9509, 0x950f, 0x9511, 0x9515, 0x9517, 0x9519, 0x95c0, 0x95c2, 0x95c4, 0x95c5, 0x95c6, 0x95c7,
  0x95c9, 0x95cc, 0x95cd, 0x95ce, 0x95cf, 0x9590, 0x9591, 0x9593, 0x9595, 0x9596, 0x9597, 0x9598, 0x9599,
  0x959b, 0x9610, 0x9611, 0x9612, 0x9613, 0x9614, 0x9615, 0x9616, 0x9710, 0x9711, 0x9712, 0x9713, 0x9714,
  0x9715, 0x9440, 0x9441, 0x9442, 0x9443, 0x9444, 0x9446, 0x944a, 0x944b, 0x944c, 0x944e, 0x9450, 0x9452,
  0x9456, 0x945a, 0x945b, 0x945e, 0x9460, 0x9462, 0x946a, 0x946b, 0x947a, 0x947b, 0x9480, 0x9487, 0x9488,
  0x9489, 0x948a, 0x948f, 0x9490, 0x9491, 0x9495, 0x9498, 0x949c, 0x949e, 0x949f, 0x9540, 0x9541, 0x9542,
  0x954e, 0x954f, 0x9552, 0x9553, 0x9555, 0x9557, 0x955f, 0x94a0, 0x94a1, 0x94a3, 0x94b1, 0x94b3, 0x94b4,
  0x94b5, 0x94b9, 0x68e0, 0x68e1, 0x68e4, 0x68e5, 0x68e8, 0x68e9, 0x68f1, 0x68f2, 0x68f8, 0x68f9, 0x68fa,
  0x68fe, 0x68c0, 0x68c1, 0x68c7, 0x68c8, 0x68c9, 0x68d8, 0x68d9, 0x68da, 0x68de, 0x68a0, 0x68a1, 0x68a8,
  0x68a9, 0x68b0, 0x68b8, 0x68b9, 0x68ba, 0x68be, 0x68bf, 0x6880, 0x6888, 0x6889, 0x688a, 0x688c, 0x688d,
  0x6898, 0x6899, 0x689b, 0x689e, 0x689c, 0x689d, 0x9802, 0x9803, 0x9804, 0x9805, 0x9806, 0x9807, 0x9808,
  0x9809, 0x980a, 0x9640, 0x9641, 0x9647, 0x9648, 0x964a, 0x964b, 0x964c, 0x964e, 0x964f, 0x9642, 0x9643,
  0x9644, 0x9645, 0x9649, 0x6720, 0x6721, 0x6722, 0x6723, 0x6724, 0x6725, 0x6726, 0x6727, 0x6728, 0x6729,
  0x6738, 0x6739, 0x673e, 0x6740, 0x6741, 0x6742, 0x6743, 0x6744, 0x6745, 0x6746, 0x6747, 0x6748, 0x6749,
  0x674a, 0x6750, 0x6751, 0x6758, 0x6759, 0x675b, 0x675d, 0x675f, 0x6840, 0x6841, 0x6842, 0x6843, 0x6849,
  0x6850, 0x6858, 0x6859, 0x6760, 0x6761, 0x6762, 0x6763, 0x6764, 0x6765, 0x6766, 0x6767, 0x6768, 0x6770,
  0x6771, 0x6772, 0x6778, 0x6779, 0x677b, 0x6700, 0x6701, 0x6702, 0x6703, 0x6704, 0x6705, 0x6706, 0x6707,
  0x6708, 0x6709, 0x6718, 0x6719, 0x671c, 0x671d, 0x671f, 0x9900, 0x9901, 0x9903, 0x9904, 0x9905, 0x9906,
  0x9907, 0x9908, 0x9909, 0x990a, 0x990b, 0x990c, 0x990d, 0x990e, 0x990f, 0x9910, 0x9913, 0x9917, 0x9918,
  0x9919, 0x9990, 0x9991, 0x9992, 0x9993, 0x9994, 0x9995, 0x9996, 0x9997, 0x9998, 0x9999, 0x999a, 0x999b,
  0x999c, 0x999d, 0x99a0, 0x99a2, 0x99a4
    // clang-format on
};

// List of NVidia Telsa GPU known to have broken NV12 rendering.
static const DWORD sNVIDIABrokenNV12[] = {
    // clang-format off
  0x0191, 0x0193, 0x0194, 0x0197, 0x019d, 0x019e, // G80
  0x0400, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0406, 0x0407, 0x0408, 0x0409, // G84
  0x040a, 0x040b, 0x040c, 0x040d, 0x040e, 0x040f,
  0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, // G86
  0x042a, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f,
  0x0410, 0x0600, 0x0601, 0x0602, 0x0603, 0x0604, 0x0605, 0x0606, 0x0607, 0x0608, // G92
  0x0609, 0x060a, 0x060b, 0x060c, 0x060f, 0x0610, 0x0611, 0x0612, 0x0613, 0x0614,
  0x0615, 0x0617, 0x0618, 0x0619, 0x061a, 0x061b, 0x061c, 0x061d, 0x061e, 0x061f, // G94
  0x0621, 0x0622, 0x0623, 0x0625, 0x0626, 0x0627, 0x0628, 0x062a, 0x062b, 0x062c,
  0x062d, 0x062e, 0x0631, 0x0635, 0x0637, 0x0638, 0x063a,
  0x0640, 0x0641, 0x0643, 0x0644, 0x0645, 0x0646, 0x0647, 0x0648, 0x0649, 0x064a, // G96
  0x064b, 0x064c, 0x0651, 0x0652, 0x0653, 0x0654, 0x0655, 0x0656, 0x0658, 0x0659,
  0x065a, 0x065b, 0x065c, 0x065f,
  0x06e0, 0x06e1, 0x06e2, 0x06e3, 0x06e4, 0x06e6, 0x06e7, 0x06e8, 0x06e9, 0x06ea, // G98
  0x06eb, 0x06ec, 0x06ef, 0x06f1, 0x06f8, 0x06f9, 0x06fa, 0x06fb, 0x06fd, 0x06ff,
  0x05e0, 0x05e1, 0x05e2, 0x05e3, 0x05e6, 0x05e7, 0x05e9, 0x05ea, 0x05eb, 0x05ed, // G200
  0x05ee, 0x05ef,
  0x0840, 0x0844, 0x0845, 0x0846, 0x0847, 0x0848, 0x0849, 0x084a, 0x084b, 0x084c, // MCP77
  0x084d, 0x084f,
  0x0860, 0x0861, 0x0862, 0x0863, 0x0864, 0x0865, 0x0866, 0x0867, 0x0868, 0x0869, // MCP79
  0x086a, 0x086c, 0x086d, 0x086e, 0x086f, 0x0870, 0x0871, 0x0872, 0x0873, 0x0874,
  0x0876, 0x087a, 0x087d, 0x087e, 0x087f,
  0x0ca0, 0x0ca2, 0x0ca3, 0x0ca2, 0x0ca4, 0x0ca5, 0x0ca7, 0x0ca9, 0x0cac, 0x0caf, // GT215
  0x0cb0, 0x0cb1, 0x0cbc,
  0x0a20, 0x0a22, 0x0a23, 0x0a26, 0x0a27, 0x0a28, 0x0a29, 0x0a2a, 0x0a2b, 0x0a2c, // GT216
  0x0a2d, 0x0a32, 0x0a34, 0x0a35, 0x0a38, 0x0a3c,
  0x0a60, 0x0a62, 0x0a63, 0x0a64, 0x0a65, 0x0a66, 0x0a67, 0x0a68, 0x0a69, 0x0a6a, // GT218
  0x0a6c, 0x0a6e, 0x0a6f, 0x0a70, 0x0a71, 0x0a72, 0x0a73, 0x0a74, 0x0a75, 0x0a76,
  0x0a78, 0x0a7a, 0x0a7c, 0x10c0, 0x10c3, 0x10c5, 0x10d8
    // clang-format on
};

extern mozilla::LazyLogModule sPDMLog;
#define LOG(...) MOZ_LOG(sPDMLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#define LOGV(...) MOZ_LOG(sPDMLog, mozilla::LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {

using layers::D3D11RecycleAllocator;
using layers::D3D11ShareHandleImage;
using layers::Image;
using layers::ImageContainer;
using namespace layers;
using namespace gfx;

StaticRefPtr<ID3D11Device> sDevice;
StaticMutex sDeviceMutex;

void GetDXVA2ExtendedFormatFromMFMediaType(IMFMediaType* pType,
                                           DXVA2_ExtendedFormat* pFormat) {
  // Get the interlace mode.
  MFVideoInterlaceMode interlace = MFVideoInterlaceMode(MFGetAttributeUINT32(
      pType, MF_MT_INTERLACE_MODE, MFVideoInterlace_Unknown));

  if (interlace == MFVideoInterlace_MixedInterlaceOrProgressive) {
    pFormat->SampleFormat = DXVA2_SampleFieldInterleavedEvenFirst;
  } else {
    pFormat->SampleFormat = UINT(interlace);
  }

  pFormat->VideoChromaSubsampling = MFGetAttributeUINT32(
      pType, MF_MT_VIDEO_CHROMA_SITING, MFVideoChromaSubsampling_Unknown);
  pFormat->NominalRange = MFGetAttributeUINT32(pType, MF_MT_VIDEO_NOMINAL_RANGE,
                                               MFNominalRange_Unknown);
  pFormat->VideoTransferMatrix = MFGetAttributeUINT32(
      pType, MF_MT_YUV_MATRIX, MFVideoTransferMatrix_Unknown);
  pFormat->VideoLighting = MFGetAttributeUINT32(pType, MF_MT_VIDEO_LIGHTING,
                                                MFVideoLighting_Unknown);
  pFormat->VideoPrimaries = MFGetAttributeUINT32(pType, MF_MT_VIDEO_PRIMARIES,
                                                 MFVideoPrimaries_Unknown);
  pFormat->VideoTransferFunction = MFGetAttributeUINT32(
      pType, MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_Unknown);
}

HRESULT ConvertMFTypeToDXVAType(IMFMediaType* pType, DXVA2_VideoDesc* pDesc) {
  ZeroMemory(pDesc, sizeof(*pDesc));

  // The D3D format is the first DWORD of the subtype GUID.
  GUID subtype = GUID_NULL;
  HRESULT hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
  pDesc->Format = (D3DFORMAT)subtype.Data1;

  UINT32 width = 0;
  UINT32 height = 0;
  hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
  NS_ENSURE_TRUE(width <= MAX_VIDEO_WIDTH, E_FAIL);
  NS_ENSURE_TRUE(height <= MAX_VIDEO_HEIGHT, E_FAIL);
  pDesc->SampleWidth = width;
  pDesc->SampleHeight = height;

  UINT32 fpsNumerator = 0;
  UINT32 fpsDenominator = 0;
  if (SUCCEEDED(MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &fpsNumerator,
                                    &fpsDenominator))) {
    pDesc->InputSampleFreq.Numerator = fpsNumerator;
    pDesc->InputSampleFreq.Denominator = fpsDenominator;

    GetDXVA2ExtendedFormatFromMFMediaType(pType, &pDesc->SampleFormat);
    pDesc->OutputFrameFreq = pDesc->InputSampleFreq;
    if ((pDesc->SampleFormat.SampleFormat ==
         DXVA2_SampleFieldInterleavedEvenFirst) ||
        (pDesc->SampleFormat.SampleFormat ==
         DXVA2_SampleFieldInterleavedOddFirst)) {
      pDesc->OutputFrameFreq.Numerator *= 2;
    }
  }

  return S_OK;
}

// All GUIDs other than Intel ClearVideo can be found here:
// https://docs.microsoft.com/en-us/windows/win32/medfound/direct3d-12-video-guids
// VLD = Variable-length decoder, FGT = Film grain technology
static const GUID DXVA2_ModeH264_VLD_NoFGT = {
    0x1b81be68,
    0xa0c7,
    0x11d3,
    {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};

// Also known as DXVADDI_Intel_ModeH264_E here:
// https://www.intel.com/content/dam/develop/external/us/en/documents/h264-avc-x4500-acceration-esardell-157713.pdf
// Named based on the fact that this is only supported on older ClearVideo
// Intel decoding hardware.
static const GUID DXVA2_Intel_ClearVideo_ModeH264_VLD_NoFGT = {
    0x604F8E68,
    0x4951,
    0x4c54,
    {0x88, 0xFE, 0xAB, 0xD2, 0x5C, 0x15, 0xB3, 0xD6}};

// VP8 profiles
static const GUID DXVA2_ModeVP8_VLD = {
    0x90b899ea,
    0x3a62,
    0x4705,
    {0x88, 0xb3, 0x8d, 0xf0, 0x4b, 0x27, 0x44, 0xe7}};

// VP9 profiles
static const GUID DXVA2_ModeVP9_VLD_Profile0 = {
    0x463707f8,
    0xa1d0,
    0x4585,
    {0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e}};

static const GUID DXVA2_ModeVP9_VLD_10bit_Profile2 = {
    0xa4c749ef,
    0x6ecf,
    0x48aa,
    {0x84, 0x48, 0x50, 0xa7, 0xa1, 0x16, 0x5f, 0xf7}};

// AV1 profiles
static const GUID DXVA2_ModeAV1_VLD_Profile0 = {
    0xb8be4ccb,
    0xcf53,
    0x46ba,
    {0x8d, 0x59, 0xd6, 0xb8, 0xa6, 0xda, 0x5d, 0x2a}};

static const GUID DXVA2_ModeAV1_VLD_Profile1 = {
    0x6936ff0f,
    0x45b1,
    0x4163,
    {0x9c, 0xc1, 0x64, 0x6e, 0xf6, 0x94, 0x61, 0x08}};

static const GUID DXVA2_ModeAV1_VLD_Profile2 = {
    0x0c5f2aa1,
    0xe541,
    0x4089,
    {0xbb, 0x7b, 0x98, 0x11, 0x0a, 0x19, 0xd7, 0xc8}};

static const GUID DXVA2_ModeAV1_VLD_12bit_Profile2 = {
    0x17127009,
    0xa00f,
    0x4ce1,
    {0x99, 0x4e, 0xbf, 0x40, 0x81, 0xf6, 0xf3, 0xf0}};

static const GUID DXVA2_ModeAV1_VLD_12bit_Profile2_420 = {
    0x2d80bed6,
    0x9cac,
    0x4835,
    {0x9e, 0x91, 0x32, 0x7b, 0xbc, 0x4f, 0x9e, 0xe8}};

// D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN
static const GUID DXVA2_ModeHEVC_VLD_MAIN = {
    0x5b11d51b,
    0x2f4c,
    0x4452,
    {0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0}};

// D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10
static const GUID DXVA2_ModeHEVC_VLD_MAIN10 = {
    0x107af0e0,
    0xef1a,
    0x4d19,
    {0xab, 0xa8, 0x67, 0xa1, 0x63, 0x07, 0x3d, 0x13}};

static const char* DecoderGUIDToStr(const GUID& aGuid) {
  if (aGuid == DXVA2_ModeH264_VLD_NoFGT) {
    return "H264";
  }
  if (aGuid == DXVA2_Intel_ClearVideo_ModeH264_VLD_NoFGT) {
    return "Intel H264";
  }
  if (aGuid == DXVA2_ModeVP8_VLD) {
    return "VP8";
  }
  if (aGuid == DXVA2_ModeVP9_VLD_Profile0) {
    return "VP9 Profile0";
  }
  if (aGuid == DXVA2_ModeVP9_VLD_10bit_Profile2) {
    return "VP9 10bits Profile2";
  }
  if (aGuid == DXVA2_ModeAV1_VLD_Profile0) {
    return "AV1 Profile0";
  }
  if (aGuid == DXVA2_ModeAV1_VLD_Profile1) {
    return "AV1 Profile1";
  }
  if (aGuid == DXVA2_ModeAV1_VLD_Profile2) {
    return "AV1 Profile2";
  }
  if (aGuid == DXVA2_ModeAV1_VLD_12bit_Profile2) {
    return "AV1 12bits Profile2";
  }
  if (aGuid == DXVA2_ModeAV1_VLD_12bit_Profile2_420) {
    return "AV1 12bits Profile2 420";
  }
  if (aGuid == DXVA2_ModeHEVC_VLD_MAIN) {
    return "HEVC main";
  }
  if (aGuid == DXVA2_ModeHEVC_VLD_MAIN10) {
    return "HEVC main10";
  }
  return "none";
}

// Count of the number of DXVAManager's we've created. This is also the
// number of videos we're decoding with DXVA. Use on main thread only.
static Atomic<uint32_t> sDXVAVideosCount(0);

// This class's functions are not thread-safe, please use them carefully.
// TODO : make this class better in bug1932998.
class D3D11DXVA2Manager : public DXVA2Manager {
 public:
  D3D11DXVA2Manager();
  virtual ~D3D11DXVA2Manager();

  HRESULT Init(layers::KnowsCompositor* aKnowsCompositor,
               nsACString& aFailureReason, ID3D11Device* aDevice);
  HRESULT InitInternal(layers::KnowsCompositor* aKnowsCompositor,
                       nsACString& aFailureReason, ID3D11Device* aDevice);

  IUnknown* GetDXVADeviceManager() override;

  // Copies a region (aRegion) of the video frame stored in aVideoSample
  // into an image which is returned by aOutImage.
  HRESULT CopyToImage(IMFSample* aVideoSample, const gfx::IntRect& aRegion,
                      Image** aOutImage) override;
  HRESULT CopyToImage(ID3D11Texture2D* aVideoSample, UINT aSurfaceIndex,
                      const gfx::IntRect& aRegion,
                      layers::Image** aOutImage) override;

  HRESULT WrapTextureWithImage(IMFSample* aVideoSample,
                               const gfx::IntRect& aRegion,
                               layers::Image** aOutImage) override;

  HRESULT WrapTextureWithImage(D3D11TextureWrapper* aTextureWrapper,
                               const gfx::IntRect& aRegion,
                               layers::Image** aOutImage) override;

  HRESULT ConfigureForSize(IMFMediaType* aInputType,
                           gfx::YUVColorSpace aColorSpace,
                           gfx::ColorRange aColorRange,
                           gfx::ColorDepth aColorDepth, uint32_t aWidth,
                           uint32_t aHeight) override;
  HRESULT ConfigureForSize(gfx::SurfaceFormat aSurfaceFormat,
                           gfx::YUVColorSpace aColorSpace,
                           gfx::ColorRange aColorRange,
                           gfx::ColorDepth aColorDepth, uint32_t aWidth,
                           uint32_t aHeight) override;

  bool IsD3D11() override { return true; }

  bool SupportsConfig(const VideoInfo& aInfo, IMFMediaType* aInputType,
                      IMFMediaType* aOutputType) override;

  void BeforeShutdownVideoMFTDecoder() override;

  bool SupportsZeroCopyNV12Texture() override {
    if (mZeroCopyUsageInfo->SupportsZeroCopyNV12Texture() &&
        (mDevice != DeviceManagerDx::Get()->GetCompositorDevice())) {
      mZeroCopyUsageInfo->DisableZeroCopyNV12Texture();
    }
    return mZeroCopyUsageInfo->SupportsZeroCopyNV12Texture();
  }

  ID3D11Device* GetD3D11Device() override { return mDevice; }

 private:
  HRESULT CreateOutputSample(RefPtr<IMFSample>& aSample,
                             ID3D11Texture2D* aTexture);

  // This is used for check whether hw decoding is possible before using MFT for
  // decoding.
  bool CanCreateDecoder(const D3D11_VIDEO_DECODER_DESC& aDesc) const;

  void RefreshIMFSampleWrappers();
  void ReleaseAllIMFSamples();

  struct InputTextureInfo {
    InputTextureInfo(ID3D11Texture2D* aTexture, UINT aIndex,
                     const gfx::IntRect& aRegion)
        : mTexture(aTexture), mIndex(aIndex), mRegion(aRegion) {};
    ID3D11Texture2D* mTexture;
    const UINT mIndex;
    const gfx::IntRect mRegion;
  };
  HRESULT CopyTextureToImage(const InputTextureInfo& aInTexture,
                             Image** aOutImage);

  VideoProcessorD3D11* GetOrCreateVideoProcessor();

  RefPtr<ID3D11Device> mDevice;
  RefPtr<ID3D11DeviceContext> mContext;
  RefPtr<IMFDXGIDeviceManager> mDXGIDeviceManager;
  RefPtr<MFTDecoder> mTransform;
  RefPtr<D3D11RecycleAllocator> mTextureClientAllocator;
  RefPtr<layers::KnowsCompositor> mKnowsCompositor;
  RefPtr<ID3D11VideoDecoder> mDecoder;
  RefPtr<layers::SyncObjectClient> mSyncObject;
  RefPtr<VideoProcessorD3D11> mProcessor;
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  UINT mDeviceManagerToken = 0;
  RefPtr<IMFMediaType> mInputType;
  GUID mInputSubType;
  gfx::YUVColorSpace mYUVColorSpace;
  gfx::ColorRange mColorRange = gfx::ColorRange::LIMITED;
  gfx::ColorDepth mColorDepth = gfx::ColorDepth::COLOR_8;
  gfx::SurfaceFormat mSurfaceFormat;
  std::list<ThreadSafeWeakPtr<layers::IMFSampleWrapper>> mIMFSampleWrappers;
  RefPtr<layers::ZeroCopyUsageInfo> mZeroCopyUsageInfo;
  uint32_t mVendorID = 0;
  RefPtr<layers::FenceD3D11> mWriteFence;
};

bool D3D11DXVA2Manager::SupportsConfig(const VideoInfo& aInfo,
                                       IMFMediaType* aInputType,
                                       IMFMediaType* aOutputType) {
  D3D11_VIDEO_DECODER_DESC desc = {GUID_NULL, 0, 0, DXGI_FORMAT_UNKNOWN};

  HRESULT hr = MFGetAttributeSize(aInputType, MF_MT_FRAME_SIZE,
                                  &desc.SampleWidth, &desc.SampleHeight);
  NS_ENSURE_TRUE(SUCCEEDED(hr), false);
  NS_ENSURE_TRUE(desc.SampleWidth <= MAX_VIDEO_WIDTH, false);
  NS_ENSURE_TRUE(desc.SampleHeight <= MAX_VIDEO_HEIGHT, false);

  GUID subtype;
  hr = aInputType->GetGUID(MF_MT_SUBTYPE, &subtype);
  NS_ENSURE_TRUE(SUCCEEDED(hr), false);

  if (subtype == MFVideoFormat_H264) {
    // IsUnsupportedResolution is only used to work around an AMD H264 issue.
    const float framerate = [&]() {
      UINT32 numerator;
      UINT32 denominator;
      if (SUCCEEDED(MFGetAttributeRatio(aInputType, MF_MT_FRAME_RATE,
                                        &numerator, &denominator))) {
        return static_cast<float>(numerator) / denominator;
      }
      return 30.0f;
    }();
    NS_ENSURE_FALSE(
        IsUnsupportedResolution(desc.SampleWidth, desc.SampleHeight, framerate),
        false);
    NS_ENSURE_TRUE(aInfo.mColorDepth == ColorDepth::COLOR_8, false);

    RefPtr<ID3D11VideoDevice> videoDevice;
    hr = mDevice->QueryInterface(
        static_cast<ID3D11VideoDevice**>(getter_AddRefs(videoDevice)));

    GUID guids[] = {DXVA2_ModeH264_VLD_NoFGT,
                    DXVA2_Intel_ClearVideo_ModeH264_VLD_NoFGT};
    for (const GUID& guid : guids) {
      BOOL supported = false;
      hr = videoDevice->CheckVideoDecoderFormat(&guid, DXGI_FORMAT_NV12,
                                                &supported);
      if (SUCCEEDED(hr) && supported) {
        desc.Guid = guid;
        break;
      }
    }
  } else if (subtype == MFVideoFormat_VP80) {
    NS_ENSURE_TRUE(aInfo.mColorDepth == ColorDepth::COLOR_8, false);
    desc.Guid = DXVA2_ModeVP8_VLD;
  } else if (subtype == MFVideoFormat_VP90) {
    NS_ENSURE_TRUE(aInfo.mColorDepth == ColorDepth::COLOR_8 ||
                       aInfo.mColorDepth == ColorDepth::COLOR_10,
                   false);
    uint8_t profile;

    if (aInfo.mExtraData && !aInfo.mExtraData->IsEmpty()) {
      VPXDecoder::VPXStreamInfo vp9Info;
      VPXDecoder::ReadVPCCBox(vp9Info, aInfo.mExtraData);
      profile = vp9Info.mProfile;
    } else {
      // If no vpcC is present, we can't know the profile, which limits the
      // subsampling mode, but 4:2:0 is most supported so default to profiles 0
      // and 2:
      // Profile 0 = 8bit, 4:2:0
      // Profile 2 = 10/12bit, 4:2:0
      profile = aInfo.mColorDepth == ColorDepth::COLOR_8 ? 0 : 2;
    }

    switch (profile) {
      case 0:
        desc.Guid = DXVA2_ModeVP9_VLD_Profile0;
        break;
      case 2:
        desc.Guid = DXVA2_ModeVP9_VLD_10bit_Profile2;
        break;
      default:
        break;
    }
  } else if (subtype == MFVideoFormat_AV1) {
    uint8_t profile;
    bool yuv420;

    if (aInfo.mExtraData && !aInfo.mExtraData->IsEmpty()) {
      AOMDecoder::AV1SequenceInfo av1Info;
      bool hadSeqHdr;
      AOMDecoder::ReadAV1CBox(aInfo.mExtraData, av1Info, hadSeqHdr);
      profile = av1Info.mProfile;
      yuv420 = av1Info.mSubsamplingX && av1Info.mSubsamplingY;
    } else {
      // If no av1C is present, we can't get profile or subsampling mode. 4:2:0
      // subsampling is most likely to be supported in hardware, so set av1Info
      // accordingly.
      // 8bit/10bit = Main profile, 4:2:0
      // 12bit = Professional, 4:2:0
      profile = aInfo.mColorDepth == ColorDepth::COLOR_12 ? 2 : 0;
      yuv420 = true;
    }

    switch (profile) {
      case 0:
        desc.Guid = DXVA2_ModeAV1_VLD_Profile0;
        break;
      case 1:
        desc.Guid = DXVA2_ModeAV1_VLD_Profile1;
        break;
      case 2:
        MOZ_ASSERT(aInfo.mColorDepth < ColorDepth::COLOR_16);
        if (aInfo.mColorDepth == ColorDepth::COLOR_12) {
          if (yuv420) {
            desc.Guid = DXVA2_ModeAV1_VLD_12bit_Profile2_420;
          } else {
            desc.Guid = DXVA2_ModeAV1_VLD_12bit_Profile2;
          }
        } else {
          desc.Guid = DXVA2_ModeAV1_VLD_Profile2;
        }
        break;
      default:
        break;
    }
  } else if (subtype == MFVideoFormat_HEVC) {
    RefPtr<ID3D11VideoDevice> videoDevice;
    hr = mDevice->QueryInterface(
        static_cast<ID3D11VideoDevice**>(getter_AddRefs(videoDevice)));
    GUID guids[] = {DXVA2_ModeHEVC_VLD_MAIN, DXVA2_ModeHEVC_VLD_MAIN10};
    for (const GUID& guid : guids) {
      BOOL supported = false;
      hr = videoDevice->CheckVideoDecoderFormat(&guid, DXGI_FORMAT_NV12,
                                                &supported);
      if (SUCCEEDED(hr) && supported) {
        desc.Guid = guid;
        break;
      }
    }
  }
  LOG("Select %s GUID", DecoderGUIDToStr(desc.Guid));

  hr = aOutputType->GetGUID(MF_MT_SUBTYPE, &subtype);
  if (SUCCEEDED(hr)) {
    if (subtype == MFVideoFormat_NV12) {
      desc.OutputFormat = DXGI_FORMAT_NV12;
    } else if (subtype == MFVideoFormat_P010) {
      desc.OutputFormat = DXGI_FORMAT_P010;
    } else if (subtype == MFVideoFormat_P016) {
      desc.OutputFormat = DXGI_FORMAT_P016;
    }
  }

  if (desc.Guid == GUID_NULL || desc.OutputFormat == DXGI_FORMAT_UNKNOWN) {
    return false;
  }

  return CanCreateDecoder(desc);
}

D3D11DXVA2Manager::D3D11DXVA2Manager()
    : mZeroCopyUsageInfo(new layers::ZeroCopyUsageInfo) {}

D3D11DXVA2Manager::~D3D11DXVA2Manager() {}

IUnknown* D3D11DXVA2Manager::GetDXVADeviceManager() {
  MutexAutoLock lock(mLock);
  return mDXGIDeviceManager;
}
HRESULT
D3D11DXVA2Manager::Init(layers::KnowsCompositor* aKnowsCompositor,
                        nsACString& aFailureReason, ID3D11Device* aDevice) {
  if (aDevice) {
    return InitInternal(aKnowsCompositor, aFailureReason, aDevice);
  }

  HRESULT hr;
  ScopedGfxFeatureReporter reporter("DXVA2D3D11");

  hr = InitInternal(aKnowsCompositor, aFailureReason, aDevice);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  if (layers::ImageBridgeChild::GetSingleton() || !aKnowsCompositor) {
    // There's no proper KnowsCompositor for ImageBridge currently (and it
    // implements the interface), so just use that if it's available.
    mTextureClientAllocator = new D3D11RecycleAllocator(
        layers::ImageBridgeChild::GetSingleton().get(), mDevice,
        gfx::SurfaceFormat::NV12);

    if (ImageBridgeChild::GetSingleton() &&
        StaticPrefs::media_wmf_use_sync_texture_AtStartup() &&
        mDevice != DeviceManagerDx::Get()->GetCompositorDevice()) {
      // We use a syncobject to avoid the cost of the mutex lock when
      // compositing, and because it allows color conversion ocurring directly
      // from this texture DXVA does not seem to accept IDXGIKeyedMutex textures
      // as input.
      mSyncObject = layers::SyncObjectClient::CreateSyncObjectClient(
          layers::ImageBridgeChild::GetSingleton()
              ->GetTextureFactoryIdentifier()
              .mSyncHandle,
          mDevice);
    }
  } else {
    mTextureClientAllocator = new D3D11RecycleAllocator(
        aKnowsCompositor, mDevice, gfx::SurfaceFormat::NV12);
    mKnowsCompositor = aKnowsCompositor;
    if (StaticPrefs::media_wmf_use_sync_texture_AtStartup()) {
      // We use a syncobject to avoid the cost of the mutex lock when
      // compositing, and because it allows color conversion ocurring directly
      // from this texture DXVA does not seem to accept IDXGIKeyedMutex textures
      // as input.
      mSyncObject = layers::SyncObjectClient::CreateSyncObjectClient(
          aKnowsCompositor->GetTextureFactoryIdentifier().mSyncHandle, mDevice);
    }
  }
  mTextureClientAllocator->SetMaxPoolSize(5);

  glean::media::decoder_backend_used.AccumulateSingleSample(
      uint32_t(media::MediaDecoderBackend::WMFDXVA2D3D11));

  reporter.SetSuccessful();

  return S_OK;
}

HRESULT
D3D11DXVA2Manager::InitInternal(layers::KnowsCompositor* aKnowsCompositor,
                                nsACString& aFailureReason,
                                ID3D11Device* aDevice) {
  HRESULT hr;

  mDevice = aDevice;

  if (!mDevice) {
    DeviceManagerDx::DeviceFlagSet flags;
    if (aKnowsCompositor && aKnowsCompositor->UsingHardwareWebRender()) {
      flags += DeviceManagerDx::DeviceFlag::isHardwareWebRenderInUse;
    }
    mDevice = gfx::DeviceManagerDx::Get()->CreateDecoderDevice(flags);
    if (!mDevice) {
      aFailureReason.AssignLiteral("Failed to create D3D11 device for decoder");
      return E_FAIL;
    }
  }

  // XXX enable fence
  const bool useFence = false;
  if (useFence) {
    mWriteFence = layers::FenceD3D11::Create(mDevice);
  }

  RefPtr<ID3D10Multithread> mt;
  hr = mDevice->QueryInterface((ID3D10Multithread**)getter_AddRefs(mt));
  NS_ENSURE_TRUE(SUCCEEDED(hr) && mt, hr);
  mt->SetMultithreadProtected(TRUE);

  mDevice->GetImmediateContext(getter_AddRefs(mContext));

  hr = wmf::MFCreateDXGIDeviceManager(&mDeviceManagerToken,
                                      getter_AddRefs(mDXGIDeviceManager));
  if (!SUCCEEDED(hr)) {
    aFailureReason =
        nsPrintfCString("MFCreateDXGIDeviceManager failed with code %lX", hr);
    return hr;
  }

  hr = mDXGIDeviceManager->ResetDevice(mDevice, mDeviceManagerToken);
  if (!SUCCEEDED(hr)) {
    aFailureReason = nsPrintfCString(
        "IMFDXGIDeviceManager::ResetDevice failed with code %lX", hr);
    return hr;
  }

  // The IMFTransform interface used by MFTDecoder is documented to require to
  // run on an MTA thread.
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ee892371(v=vs.85).aspx#components
  // The main thread (where this function is called) is STA, not MTA.
  RefPtr<MFTDecoder> mft;
  mozilla::mscom::EnsureMTA([&]() -> void {
    mft = new MFTDecoder();
    hr = mft->Create(MFT_CATEGORY_VIDEO_PROCESSOR, MFVideoFormat_NV12,
                     MFVideoFormat_ARGB32);

    if (!SUCCEEDED(hr)) {
      aFailureReason = nsPrintfCString(
          "MFTDecoder::Create of Video Processor MFT for color conversion "
          "failed with code %lX",
          hr);
      return;
    }

    hr = mft->SendMFTMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                             ULONG_PTR(mDXGIDeviceManager.get()));
    if (!SUCCEEDED(hr)) {
      aFailureReason = nsPrintfCString(
          "MFTDecoder::SendMFTMessage(MFT_MESSAGE_"
          "SET_D3D_MANAGER) failed with code %lX",
          hr);
      return;
    }
  });

  if (!SUCCEEDED(hr)) {
    return hr;
  }
  mTransform = mft;

  RefPtr<IDXGIDevice> dxgiDevice;
  hr = mDevice->QueryInterface(
      static_cast<IDXGIDevice**>(getter_AddRefs(dxgiDevice)));
  if (!SUCCEEDED(hr)) {
    aFailureReason =
        nsPrintfCString("QI to IDXGIDevice failed with code %lX", hr);
    return hr;
  }

  RefPtr<IDXGIAdapter> adapter;
  hr = dxgiDevice->GetAdapter(adapter.StartAssignment());
  if (!SUCCEEDED(hr)) {
    aFailureReason =
        nsPrintfCString("IDXGIDevice::GetAdapter failed with code %lX", hr);
    return hr;
  }

  DXGI_ADAPTER_DESC adapterDesc;
  hr = adapter->GetDesc(&adapterDesc);
  if (!SUCCEEDED(hr)) {
    aFailureReason =
        nsPrintfCString("IDXGIAdapter::GetDesc failed with code %lX", hr);
    return hr;
  }

  mVendorID = adapterDesc.VendorId;

  if ((adapterDesc.VendorId == 0x1022 || adapterDesc.VendorId == 0x1002) &&
      !StaticPrefs::media_wmf_skip_blacklist()) {
    for (const auto& model : sAMDPreUVD4) {
      if (adapterDesc.DeviceId == model) {
        mIsAMDPreUVD4 = true;
        break;
      }
    }
  }

  if (!IsD3D11() || !XRE_IsGPUProcess() ||
      (mDevice != DeviceManagerDx::Get()->GetCompositorDevice())) {
    mZeroCopyUsageInfo->DisableZeroCopyNV12Texture();
  }

  return S_OK;
}

HRESULT
D3D11DXVA2Manager::CreateOutputSample(RefPtr<IMFSample>& aSample,
                                      ID3D11Texture2D* aTexture) {
  RefPtr<IMFSample> sample;
  HRESULT hr = wmf::MFCreateSample(getter_AddRefs(sample));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  RefPtr<IMFMediaBuffer> buffer;
  hr = wmf::MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), aTexture, 0,
                                      FALSE, getter_AddRefs(buffer));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  hr = sample->AddBuffer(buffer);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  aSample = sample;
  return S_OK;
}

HRESULT
D3D11DXVA2Manager::CopyToImage(IMFSample* aVideoSample,
                               const gfx::IntRect& aRegion, Image** aOutImage) {
  NS_ENSURE_TRUE(aVideoSample, E_POINTER);
  NS_ENSURE_TRUE(aOutImage, E_POINTER);
  MOZ_ASSERT(mTextureClientAllocator);

  // Retrieve the DXGI_FORMAT for the current video sample.
  RefPtr<IMFMediaBuffer> buffer;
  HRESULT hr = aVideoSample->GetBufferByIndex(0, getter_AddRefs(buffer));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  RefPtr<IMFDXGIBuffer> dxgiBuf;
  hr = buffer->QueryInterface((IMFDXGIBuffer**)getter_AddRefs(dxgiBuf));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  RefPtr<ID3D11Texture2D> inputTexture;
  hr = dxgiBuf->GetResource(__uuidof(ID3D11Texture2D),
                            getter_AddRefs(inputTexture));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  UINT index;
  dxgiBuf->GetSubresourceIndex(&index);

  InputTextureInfo info(inputTexture, index, aRegion);
  return CopyTextureToImage(info, aOutImage);
}

HRESULT D3D11DXVA2Manager::CopyToImage(ID3D11Texture2D* aInputTexture,
                                       UINT aSurfaceIndex,
                                       const gfx::IntRect& aRegion,
                                       layers::Image** aOutImage) {
  InputTextureInfo info(aInputTexture, aSurfaceIndex, aRegion);
  return CopyTextureToImage(info, aOutImage);
}

HRESULT D3D11DXVA2Manager::WrapTextureWithImage(IMFSample* aVideoSample,
                                                const gfx::IntRect& aRegion,
                                                layers::Image** aOutImage) {
  NS_ENSURE_TRUE(aVideoSample, E_POINTER);
  NS_ENSURE_TRUE(aOutImage, E_POINTER);

  RefPtr<IMFMediaBuffer> buffer;
  HRESULT hr = aVideoSample->GetBufferByIndex(0, getter_AddRefs(buffer));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  RefPtr<IMFDXGIBuffer> dxgiBuf;
  hr = buffer->QueryInterface((IMFDXGIBuffer**)getter_AddRefs(dxgiBuf));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  RefPtr<ID3D11Texture2D> texture;
  hr = dxgiBuf->GetResource(__uuidof(ID3D11Texture2D), getter_AddRefs(texture));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);

  UINT arrayIndex;
  dxgiBuf->GetSubresourceIndex(&arrayIndex);

  RefreshIMFSampleWrappers();

  RefPtr<D3D11TextureIMFSampleImage> image = new D3D11TextureIMFSampleImage(
      aVideoSample, texture, arrayIndex, gfx::IntSize(mWidth, mHeight), aRegion,
      ToColorSpace2(mYUVColorSpace), mColorRange, mColorDepth);
  image->AllocateTextureClient(mKnowsCompositor, mZeroCopyUsageInfo,
                               mWriteFence);

  RefPtr<IMFSampleWrapper> wrapper = image->GetIMFSampleWrapper();
  ThreadSafeWeakPtr<IMFSampleWrapper> weak(wrapper);
  mIMFSampleWrappers.push_back(weak);

  image.forget(aOutImage);

  return S_OK;
}

HRESULT D3D11DXVA2Manager::WrapTextureWithImage(
    D3D11TextureWrapper* aTextureWrapper, const gfx::IntRect& aRegion,
    layers::Image** aOutImage) {
  NS_ENSURE_TRUE(aOutImage, E_POINTER);
  RefPtr<D3D11TextureAVFrameImage> image = new D3D11TextureAVFrameImage(
      aTextureWrapper, gfx::IntSize(mWidth, mHeight), aRegion,
      ToColorSpace2(mYUVColorSpace), mColorRange, mColorDepth);
  image->AllocateTextureClient(mKnowsCompositor, mZeroCopyUsageInfo,
                               mWriteFence);
  image.forget(aOutImage);
  return S_OK;
}

void D3D11DXVA2Manager::RefreshIMFSampleWrappers() {
  for (auto it = mIMFSampleWrappers.begin(); it != mIMFSampleWrappers.end();) {
    auto wrapper = RefPtr<IMFSampleWrapper>(*it);
    if (!wrapper) {
      // wrapper is already destroyed.
      it = mIMFSampleWrappers.erase(it);
      continue;
    }
    it++;
  }
}

void D3D11DXVA2Manager::ReleaseAllIMFSamples() {
  for (auto it = mIMFSampleWrappers.begin(); it != mIMFSampleWrappers.end();
       it++) {
    RefPtr<IMFSampleWrapper> wrapper = RefPtr<IMFSampleWrapper>(*it);
    if (wrapper) {
      wrapper->ClearVideoSample();
    }
  }
}

void D3D11DXVA2Manager::BeforeShutdownVideoMFTDecoder() {
  ReleaseAllIMFSamples();
}

HRESULT
D3D11DXVA2Manager::ConfigureForSize(IMFMediaType* aInputType,
                                    gfx::YUVColorSpace aColorSpace,
                                    gfx::ColorRange aColorRange,
                                    gfx::ColorDepth aColorDepth,
                                    uint32_t aWidth, uint32_t aHeight) {
  GUID subType = {0};
  HRESULT hr = aInputType->GetGUID(MF_MT_SUBTYPE, &subType);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  if (subType == mInputSubType && aWidth == mWidth && aHeight == mHeight &&
      mYUVColorSpace == aColorSpace && mColorRange == aColorRange &&
      mColorDepth == aColorDepth) {
    // If the media type hasn't changed, don't reconfigure.
    return S_OK;
  }

  // Create a copy of our input type.
  RefPtr<IMFMediaType> inputType;
  hr = wmf::MFCreateMediaType(getter_AddRefs(inputType));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
  hr = aInputType->CopyAllItems(inputType);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, aWidth, aHeight);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  RefPtr<IMFAttributes> attr;
  mozilla::mscom::EnsureMTA(
      [&]() -> void { attr = mTransform->GetAttributes(); });
  NS_ENSURE_TRUE(attr != nullptr, E_FAIL);

  hr = attr->SetUINT32(MF_XVP_PLAYBACK_MODE, TRUE);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  hr = attr->SetUINT32(MF_LOW_LATENCY, FALSE);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  RefPtr<IMFMediaType> outputType;
  hr = wmf::MFCreateMediaType(getter_AddRefs(outputType));
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  hr = E_FAIL;
  mozilla::mscom::EnsureMTA([&]() -> void {
    hr = mTransform->SetMediaTypes(
        inputType, outputType, [aWidth, aHeight](IMFMediaType* aOutput) {
          HRESULT hr = aOutput->SetUINT32(MF_MT_INTERLACE_MODE,
                                          MFVideoInterlace_Progressive);
          NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
          hr = aOutput->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
          NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
          hr = MFSetAttributeSize(aOutput, MF_MT_FRAME_SIZE, aWidth, aHeight);
          NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

          return S_OK;
        });
  });
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  const bool isSizeChanged = (mWidth != aWidth) || (mHeight != aHeight);
  mWidth = aWidth;
  mHeight = aHeight;
  mInputType = inputType;
  mInputSubType = subType;
  mYUVColorSpace = aColorSpace;
  mColorRange = aColorRange;
  mColorDepth = aColorDepth;
  if (mTextureClientAllocator) {
    mSurfaceFormat = [&]() {
      if (subType == MFVideoFormat_NV12) {
        return gfx::SurfaceFormat::NV12;
      } else if (subType == MFVideoFormat_P010) {
        return gfx::SurfaceFormat::P010;
      } else if (subType == MFVideoFormat_P016) {
        return gfx::SurfaceFormat::P016;
      } else {
        MOZ_ASSERT_UNREACHABLE("Unexpected texture type");
        return gfx::SurfaceFormat::NV12;
      }
    }();
    mTextureClientAllocator->SetPreferredSurfaceFormat(mSurfaceFormat);
  }
  // Reconfig video processor as well
  if (isSizeChanged && mProcessor) {
    hr = mProcessor->Init(gfx::IntSize(mWidth, mHeight));
    NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
  }
  LOG("Configured D3D11DXVA2Manager, size=[%u,%u], colorSpace=%hhu, "
      "colorRange=%hhu, colorDepth=%hhu",
      mWidth, mHeight, static_cast<uint8_t>(mYUVColorSpace),
      static_cast<uint8_t>(mColorRange), static_cast<uint8_t>(mColorDepth));
  return S_OK;
}

HRESULT
D3D11DXVA2Manager::ConfigureForSize(gfx::SurfaceFormat aSurfaceFormat,
                                    gfx::YUVColorSpace aColorSpace,
                                    gfx::ColorRange aColorRange,
                                    gfx::ColorDepth aColorDepth,
                                    uint32_t aWidth, uint32_t aHeight) {
  if (aWidth == mWidth && aHeight == mHeight && mYUVColorSpace == aColorSpace &&
      mColorRange == aColorRange && aSurfaceFormat == mSurfaceFormat &&
      mColorDepth == aColorDepth) {
    // No need to reconfigure if nothing changes.
    return S_OK;
  }

  const bool isSizeChanged = (mWidth != aWidth) || (mHeight != aHeight);
  mWidth = aWidth;
  mHeight = aHeight;
  mYUVColorSpace = aColorSpace;
  mColorRange = aColorRange;
  mColorDepth = aColorDepth;
  mSurfaceFormat = aSurfaceFormat;
  if (mTextureClientAllocator) {
    mTextureClientAllocator->SetPreferredSurfaceFormat(mSurfaceFormat);
  }
  // Reconfig video processor as well
  if (isSizeChanged && mProcessor) {
    mProcessor->Init(gfx::IntSize(mWidth, mHeight));
  }
  LOG("Configured D3D11DXVA2Manager, size=[%u,%u], colorSpace=%hhu, "
      "colorRange=%hhu, colorDepth=%hhu, surfaceFormat=%hhd",
      mWidth, mHeight, static_cast<uint8_t>(mYUVColorSpace),
      static_cast<uint8_t>(mColorRange), static_cast<uint8_t>(mColorDepth),
      static_cast<uint8_t>(mSurfaceFormat));
  return S_OK;
}

bool D3D11DXVA2Manager::CanCreateDecoder(
    const D3D11_VIDEO_DECODER_DESC& aDesc) const {
  RefPtr<ID3D11VideoDevice> videoDevice;
  HRESULT hr = mDevice->QueryInterface(
      static_cast<ID3D11VideoDevice**>(getter_AddRefs(videoDevice)));
  if (FAILED(hr)) {
    LOG("Failed to query ID3D11VideoDevice!");
    return false;
  }

  UINT configCount = 0;
  hr = videoDevice->GetVideoDecoderConfigCount(&aDesc, &configCount);
  if (FAILED(hr)) {
    LOG("Failed to get decoder config count!");
    return false;
  }

  for (UINT i = 0; i < configCount; i++) {
    D3D11_VIDEO_DECODER_CONFIG config;
    hr = videoDevice->GetVideoDecoderConfig(&aDesc, i, &config);
    if (SUCCEEDED(hr)) {
      RefPtr<ID3D11VideoDecoder> decoder;
      hr = videoDevice->CreateVideoDecoder(&aDesc, &config,
                                           decoder.StartAssignment());
      return decoder != nullptr;
    }
  }
  return false;
}

/* static */
DXVA2Manager* DXVA2Manager::CreateD3D11DXVA(
    layers::KnowsCompositor* aKnowsCompositor, nsACString& aFailureReason,
    ID3D11Device* aDevice, DXVA2Usage aUsage) {
  // DXVA processing takes up a lot of GPU resources, so limit the number of
  // videos we use DXVA with at any one time.
  uint32_t dxvaLimit = StaticPrefs::media_wmf_dxva_max_videos();

  if (sDXVAVideosCount == dxvaLimit && aUsage == DXVA2Usage::Playback) {
    aFailureReason.AssignLiteral("Too many DXVA videos playing");
    return nullptr;
  }

  UniquePtr<D3D11DXVA2Manager> manager(new D3D11DXVA2Manager());
  HRESULT hr = manager->Init(aKnowsCompositor, aFailureReason, aDevice);
  NS_ENSURE_TRUE(SUCCEEDED(hr), nullptr);

  return manager.release();
}

DXVA2Manager::DXVA2Manager() : mLock("DXVA2Manager") { ++sDXVAVideosCount; }

DXVA2Manager::~DXVA2Manager() { --sDXVAVideosCount; }

bool DXVA2Manager::IsUnsupportedResolution(const uint32_t& aWidth,
                                           const uint32_t& aHeight,
                                           const float& aFramerate) const {
  // AMD cards with UVD3 or earlier perform poorly trying to decode 1080p60 in
  // hardware, so use software instead. Pick 45 as an arbitrary upper bound for
  // the framerate we can handle.
  return !StaticPrefs::media_wmf_amd_highres_enabled() && mIsAMDPreUVD4 &&
         (aWidth >= 1920 || aHeight >= 1088) && aFramerate > 45;
}

/* static */
bool DXVA2Manager::IsNV12Supported(uint32_t aVendorID, uint32_t aDeviceID,
                                   const nsAString& aDriverVersionString) {
  if (aVendorID == 0x1022 || aVendorID == 0x1002) {
    // AMD
    // Block old cards regardless of driver version.
    for (const auto& model : sAMDPreUVD4) {
      if (aDeviceID == model) {
        return false;
      }
    }
    // AMD driver earlier than 21.19.411.0 have bugs in their handling of NV12
    // surfaces.
    uint64_t driverVersion;
    if (!widget::ParseDriverVersion(aDriverVersionString, &driverVersion) ||
        driverVersion < widget::V(21, 19, 411, 0)) {
      return false;
    }
  } else if (aVendorID == 0x10DE) {
    // NVidia
    for (const auto& model : sNVIDIABrokenNV12) {
      if (aDeviceID == model) {
        return false;
      }
    }
  }
  return true;
}

HRESULT D3D11DXVA2Manager::CopyTextureToImage(
    const InputTextureInfo& aInTexture, Image** aOutImage) {
  MOZ_DIAGNOSTIC_ASSERT(aInTexture.mTexture);

  D3D11_TEXTURE2D_DESC inDesc;
  aInTexture.mTexture->GetDesc(&inDesc);

  RefPtr<D3D11ShareHandleImage> image = new D3D11ShareHandleImage(
      gfx::IntSize(mWidth, mHeight), aInTexture.mRegion,
      ToColorSpace2(mYUVColorSpace), mColorRange, mColorDepth);

  if (!image->AllocateTexture(mTextureClientAllocator, mDevice)) {
    LOG("Failed to allocate texture!");
    return E_FAIL;
  }

  RefPtr<TextureClient> client =
      image->GetTextureClient(ImageBridgeChild::GetSingleton().get());
  if (!client) {
    LOG("Failed to get texture client!");
    return E_FAIL;
  }

  RefPtr<ID3D11Texture2D> texture = image->GetTexture();
  D3D11_TEXTURE2D_DESC outDesc;
  texture->GetDesc(&outDesc);

  LOGV("CopyTexture, inTextureFormat=%d, outTextureFormat=%d", inDesc.Format,
       outDesc.Format);

  RefPtr<IDXGIKeyedMutex> mutex;
  texture->QueryInterface((IDXGIKeyedMutex**)getter_AddRefs(mutex));

  HRESULT hr;
  {
    AutoTextureLock(mutex, hr, 2000);
    if (mutex && (FAILED(hr) || hr == WAIT_TIMEOUT || hr == WAIT_ABANDONED)) {
      LOG("Failed to require texture lock");
      return hr;
    }

    if (!mutex && mDevice != DeviceManagerDx::Get()->GetCompositorDevice() &&
        !mSyncObject) {
      LOG("No sync object!");
      return E_FAIL;
    }

    UINT height = std::min(inDesc.Height, outDesc.Height);
    PerformanceRecorder<PlaybackStage> perfRecorder(
        MediaStage::CopyDecodedVideo, height);
    // The D3D11TextureClientAllocator may return a different texture format
    // than preferred. In which case the destination texture will be BGRA32.
    // Eg. when NV12 is blocked by Gfx.
    if (outDesc.Format == inDesc.Format) {
      // Our video frame is stored in a non-sharable ID3D11Texture2D. We need
      // to create a copy of that frame as a sharable resource, save its share
      // handle, and put that handle into the rendering pipeline.
      UINT width = std::min(inDesc.Width, outDesc.Width);
      D3D11_BOX srcBox = {0, 0, 0, width, height, 1};
      mContext->CopySubresourceRegion(texture, 0, 0, 0, 0, aInTexture.mTexture,
                                      aInTexture.mIndex, &srcBox);
    } else {
      // Convert YUV to RGB.
      auto* processor = GetOrCreateVideoProcessor();
      if (!processor) {
        LOG("Failed to get a video processor");
        return E_FAIL;
      }
      VideoProcessorD3D11::InputTextureInfo info(ToColorSpace2(mYUVColorSpace),
                                                 mColorRange, aInTexture.mIndex,
                                                 aInTexture.mTexture);
      if (!processor->CallVideoProcessorBlt(info, texture.get())) {
        LOG("Failed on CallVideoProcessorBlt!");
        return E_FAIL;
      }
    }
    perfRecorder.Record();
  }

  auto* textureData = client->GetInternalData()->AsD3D11TextureData();
  auto* fencesHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
  MOZ_ASSERT(textureData);
  const bool useFence =
      textureData && textureData->mFencesHolderId.isSome() && fencesHolderMap;
  if (useFence) {
    textureData->IncrementAndSignalWriteFence();
  } else if (!mutex &&
             mDevice != DeviceManagerDx::Get()->GetCompositorDevice() &&
             mSyncObject) {
    static StaticMutex sMutex MOZ_UNANNOTATED;
    // Ensure that we only ever attempt to synchronise via the sync object
    // serially as when using the same D3D11 device for multiple video decoders
    // it can lead to deadlocks.
    StaticMutexAutoLock lock(sMutex);
    // It appears some race-condition may allow us to arrive here even when
    // mSyncObject is null. It's better to avoid that crash.
    client->SyncWithObject(mSyncObject);
    if (!mSyncObject->Synchronize(true)) {
      return DXGI_ERROR_DEVICE_RESET;
    }
  }

  image.forget(aOutImage);
  return S_OK;
}

VideoProcessorD3D11* D3D11DXVA2Manager::GetOrCreateVideoProcessor() {
  if (mProcessor) {
    return mProcessor;
  }
  mProcessor = VideoProcessorD3D11::Create(mDevice);
  if (!mProcessor) {
    LOG("Failed to create video processor D3D11");
    return nullptr;
  }
  HRESULT hr = mProcessor->Init(gfx::IntSize(mWidth, mHeight));
  if (FAILED(hr)) {
    mProcessor = nullptr;
    LOG("Failed to init video processor D3D11, hr=%lx", hr);
  }
  return mProcessor;
}

}  // namespace mozilla

#undef LOG
#undef LOGV
