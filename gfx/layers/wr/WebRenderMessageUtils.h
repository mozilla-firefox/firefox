/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WEBRENDERMESSAGEUTILS_H
#define GFX_WEBRENDERMESSAGEUTILS_H

#include "chrome/common/ipc_message_utils.h"

#include "ipc/EnumSerializer.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/ParamTraits_TiedFields.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/dom/MediaIPCUtils.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::wr::ByteBuffer> {
  typedef mozilla::wr::ByteBuffer paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mLength);
    aWriter->WriteBytes(aParam.mData, aParam.mLength);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    size_t length;
    return ReadParam(aReader, &length) && aResult->Allocate(length) &&
           aReader->ReadBytesInto(aResult->mData, length);
  }
};

template <>
struct ParamTraits<mozilla::wr::ImageDescriptor> {
  typedef mozilla::wr::ImageDescriptor paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.format);
    WriteParam(aWriter, aParam.width);
    WriteParam(aWriter, aParam.height);
    WriteParam(aWriter, aParam.stride);
    WriteParam(aWriter, aParam.opacity);
  }

  static ReadResult<paramType> Read(MessageReader* aReader) {
    mozilla::wr::ImageFormat format;
    int32_t width;
    int32_t height;
    int32_t stride;
    mozilla::wr::OpacityType opacity;
    if (!ReadParam(aReader, &format) || !ReadParam(aReader, &width) ||
        !ReadParam(aReader, &height) || !ReadParam(aReader, &stride) ||
        !ReadParam(aReader, &opacity)) {
      return {};
    }
    if (width < 0 || height < 0 || stride < 0) {
      return {};
    }
    if (stride != 0) {
      int bpp = mozilla::gfx::BytesPerPixel(
          mozilla::wr::ImageFormatToSurfaceFormat(format));
      // If stride specified, width must be > 0.
      if (bpp <= 0 || width <= 0 || stride / bpp < width) {
        return {};
      }
    }
    return paramType(mozilla::gfx::IntSize(width, height), stride, format,
                     opacity);
  }
};

template <>
struct ParamTraits<mozilla::wr::GeckoDisplayListType::Tag>
    : public ContiguousEnumSerializer<
          mozilla::wr::GeckoDisplayListType::Tag,
          mozilla::wr::GeckoDisplayListType::Tag::None,
          mozilla::wr::GeckoDisplayListType::Tag::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::GeckoDisplayListType> {
  typedef mozilla::wr::GeckoDisplayListType paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.tag);
    switch (aParam.tag) {
      case mozilla::wr::GeckoDisplayListType::Tag::None:
        break;
      case mozilla::wr::GeckoDisplayListType::Tag::Partial:
        WriteParam(aWriter, aParam.partial._0);
        break;
      case mozilla::wr::GeckoDisplayListType::Tag::Full:
        WriteParam(aWriter, aParam.full._0);
        break;
      default:
        MOZ_RELEASE_ASSERT(false, "bad enum variant");
    }
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    if (!ReadParam(aReader, &aResult->tag)) {
      return false;
    }
    switch (aResult->tag) {
      case mozilla::wr::GeckoDisplayListType::Tag::None:
        return true;
      case mozilla::wr::GeckoDisplayListType::Tag::Partial:
        return ReadParam(aReader, &aResult->partial._0);
      case mozilla::wr::GeckoDisplayListType::Tag::Full:
        return ReadParam(aReader, &aResult->full._0);
      default:
        MOZ_RELEASE_ASSERT(false, "bad enum variant");
    }
  }
};

template <>
struct ParamTraits<mozilla::wr::BuiltDisplayListDescriptor> {
  typedef mozilla::wr::BuiltDisplayListDescriptor paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.gecko_display_list_type);
    WriteParam(aWriter, aParam.builder_start_time);
    WriteParam(aWriter, aParam.builder_finish_time);
    WriteParam(aWriter, aParam.send_start_time);
    WriteParam(aWriter, aParam.total_clip_nodes);
    WriteParam(aWriter, aParam.total_spatial_nodes);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->gecko_display_list_type) &&
           ReadParam(aReader, &aResult->builder_start_time) &&
           ReadParam(aReader, &aResult->builder_finish_time) &&
           ReadParam(aReader, &aResult->send_start_time) &&
           ReadParam(aReader, &aResult->total_clip_nodes) &&
           ReadParam(aReader, &aResult->total_spatial_nodes);
  }
};

}  // namespace IPC
namespace mozilla {
template <>
inline auto TiedFields<mozilla::wr::IdNamespace>(mozilla::wr::IdNamespace& a) {
  return std::tie(a.mHandle);
}

template <>
inline auto TiedFields<mozilla::wr::ImageKey>(mozilla::wr::ImageKey& a) {
  return std::tie(a.mNamespace, a.mHandle);
}

template <>
inline auto TiedFields<mozilla::wr::BlobImageKey>(
    mozilla::wr::BlobImageKey& a) {
  return std::tie(a._0);
}

template <>
inline auto TiedFields<mozilla::wr::SnapshotImageKey>(
    mozilla::wr::SnapshotImageKey& a) {
  return std::tie(a._0);
}

template <>
inline auto TiedFields<mozilla::wr::FontKey>(mozilla::wr::FontKey& a) {
  return std::tie(a.mNamespace, a.mHandle);
}
template <>
inline auto TiedFields<mozilla::wr::FontInstanceKey>(
    mozilla::wr::FontInstanceKey& a) {
  return std::tie(a.mNamespace, a.mHandle);
}

template <>
inline auto TiedFields<mozilla::wr::FontInstanceFlags>(
    mozilla::wr::FontInstanceFlags& a) {
  return std::tie(a._0);
}

template <>
inline auto TiedFields<mozilla::wr::SyntheticItalics>(
    mozilla::wr::SyntheticItalics& a) {
  return std::tie(a.angle);
}

template <>
inline auto TiedFields<mozilla::wr::FontInstanceOptions>(
    mozilla::wr::FontInstanceOptions& a) {
  return std::tie(a.flags, a.synthetic_italics, a.render_mode, a._padding);
}

template <>
inline auto TiedFields<mozilla::wr::FontInstancePlatformOptions>(
    mozilla::wr::FontInstancePlatformOptions& a) {
#if defined(XP_WIN)
  return std::tie(a.gamma, a.contrast, a.cleartype_level);
#elif defined(XP_MACOSX)
  return std::tie(a.unused);
#else
  return std::tie(a.lcd_filter, a.hinting, a.gamma, a.enhanced_contrast);
#endif
}

template <>
inline auto TiedFields<mozilla::wr::ExternalImageId>(
    mozilla::wr::ExternalImageId& a) {
  return std::tie(a._0);
}

template <>
inline auto TiedFields<mozilla::wr::PipelineId>(mozilla::wr::PipelineId& a) {
  return std::tie(a.mNamespace, a.mHandle);
}

template <>
inline auto TiedFields<mozilla::wr::LayoutSize>(mozilla::wr::LayoutSize& a) {
  return std::tie(a.width, a.height);
}
template <>
inline auto TiedFields<mozilla::wr::LayoutRect>(mozilla::wr::LayoutRect& a) {
  return std::tie(a.min, a.max);
}
template <>
inline auto TiedFields<mozilla::wr::LayoutPoint>(mozilla::wr::LayoutPoint& a) {
  return std::tie(a.x, a.y);
}

template <>
inline auto TiedFields<mozilla::wr::InternerSubReport>(
    mozilla::wr::InternerSubReport& a) {
#define _COMMA ,
#define _(X) a.X
  return std::tie(WEBRENDER_FOR_EACH_INTERNER(_, _COMMA));
#undef _
#undef _COMMA
}

template <>
inline auto TiedFields<mozilla::wr::InterningMemoryReport>(
    mozilla::wr::InterningMemoryReport& a) {
  return std::tie(a.interners, a.data_stores);
}

template <>
inline auto TiedFields<mozilla::wr::MemoryReport>(
    mozilla::wr::MemoryReport& a) {
  // clang-format off
  return std::tie(
    a.clip_stores,
    a.hit_testers,
    a.fonts,
    a.weak_fonts,
    a.images,
    a.rasterized_blobs,
    a.shader_cache,
    a.interning,
    a.display_list,
    a.upload_staging_memory,
    a.swgl,
    a.frame_allocator,
    a.render_tasks,
    a.vertex_data_textures,
    a.render_target_textures,
    a.picture_tile_textures,
    a.atlas_textures,
    a.standalone_textures,
    a.texture_cache_structures,
    a.depth_target_textures,
    a.texture_upload_pbos,
    a.swap_chain,
    a.render_texture_hosts,
    a.upload_staging_textures
  );
  // clang-format on
}

template <>
inline auto TiedFields<mozilla::wr::RenderReasons>(
    mozilla::wr::RenderReasons& a) {
  return std::tie(a._0);
}

}  // namespace mozilla
namespace IPC {
template <>
struct ParamTraits<mozilla::wr::IdNamespace>
    : public ParamTraits_TiedFields<mozilla::wr::IdNamespace> {};

template <>
struct ParamTraits<mozilla::wr::ImageKey>
    : public ParamTraits_TiedFields<mozilla::wr::ImageKey> {};

template <>
struct ParamTraits<mozilla::wr::BlobImageKey>
    : public ParamTraits_TiedFields<mozilla::wr::BlobImageKey> {};

template <>
struct ParamTraits<mozilla::wr::SnapshotImageKey>
    : public ParamTraits_TiedFields<mozilla::wr::SnapshotImageKey> {};

template <>
struct ParamTraits<mozilla::wr::FontKey>
    : public ParamTraits_TiedFields<mozilla::wr::FontKey> {};

template <>
struct ParamTraits<mozilla::wr::FontInstanceKey>
    : public ParamTraits_TiedFields<mozilla::wr::FontInstanceKey> {};

template <>
struct ParamTraits<mozilla::wr::FontInstanceFlags>
    : public ParamTraits_TiedFields<mozilla::wr::FontInstanceFlags> {};

template <>
struct ParamTraits<mozilla::wr::SyntheticItalics>
    : public ParamTraits_TiedFields<mozilla::wr::SyntheticItalics> {};

template <>
struct ParamTraits<mozilla::wr::FontInstanceOptions>
    : public ParamTraits_TiedFields<mozilla::wr::FontInstanceOptions> {};

#if !(defined(XP_MACOSX) || defined(XP_WIN))

template <>
struct ParamTraits<mozilla::wr::FontLCDFilter>
    : public ContiguousEnumSerializer<mozilla::wr::FontLCDFilter,
                                      mozilla::wr::FontLCDFilter::None,
                                      mozilla::wr::FontLCDFilter::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::FontHinting>
    : public ContiguousEnumSerializer<mozilla::wr::FontHinting,
                                      mozilla::wr::FontHinting::None,
                                      mozilla::wr::FontHinting::Sentinel> {};

#endif  // !(defined(XP_MACOSX) || defined(XP_WIN))

template <>
struct ParamTraits<mozilla::wr::FontInstancePlatformOptions>
    : public ParamTraits_TiedFields<mozilla::wr::FontInstancePlatformOptions> {
};

template <>
struct ParamTraits<mozilla::wr::ExternalImageId>
    : public ParamTraits_TiedFields<mozilla::wr::ExternalImageId> {};

template <>
struct ParamTraits<mozilla::wr::PipelineId>
    : public ParamTraits_TiedFields<mozilla::wr::PipelineId> {};

template <>
struct ParamTraits<mozilla::wr::ImageFormat>
    : public ContiguousEnumSerializer<mozilla::wr::ImageFormat,
                                      mozilla::wr::ImageFormat::R8,
                                      mozilla::wr::ImageFormat::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::LayoutSize>
    : public ParamTraits_TiedFields<mozilla::wr::LayoutSize> {};

template <>
struct ParamTraits<mozilla::wr::LayoutRect>
    : public ParamTraits_TiedFields<mozilla::wr::LayoutRect> {};

template <>
struct ParamTraits<mozilla::wr::LayoutPoint>
    : public ParamTraits_TiedFields<mozilla::wr::LayoutPoint> {};

template <>
struct ParamTraits<mozilla::wr::ImageRendering>
    : public ContiguousEnumSerializer<mozilla::wr::ImageRendering,
                                      mozilla::wr::ImageRendering::Auto,
                                      mozilla::wr::ImageRendering::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::MixBlendMode>
    : public ContiguousEnumSerializer<mozilla::wr::MixBlendMode,
                                      mozilla::wr::MixBlendMode::Normal,
                                      mozilla::wr::MixBlendMode::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::WebRenderError>
    : public ContiguousEnumSerializer<mozilla::wr::WebRenderError,
                                      mozilla::wr::WebRenderError::INITIALIZE,
                                      mozilla::wr::WebRenderError::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::WrRotation>
    : public ContiguousEnumSerializer<mozilla::wr::WrRotation,
                                      mozilla::wr::WrRotation::Degree0,
                                      mozilla::wr::WrRotation::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::InternerSubReport>
    : public ParamTraits_TiedFields<mozilla::wr::InternerSubReport> {};

template <>
struct ParamTraits<mozilla::wr::InterningMemoryReport>
    : public ParamTraits_TiedFields<mozilla::wr::InterningMemoryReport> {};

template <>
struct ParamTraits<mozilla::wr::MemoryReport>
    : public ParamTraits_TiedFields<mozilla::wr::MemoryReport> {};

template <>
struct ParamTraits<mozilla::wr::OpacityType>
    : public ContiguousEnumSerializer<mozilla::wr::OpacityType,
                                      mozilla::wr::OpacityType::Opaque,
                                      mozilla::wr::OpacityType::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::FontRenderMode>
    : public ContiguousEnumSerializer<mozilla::wr::FontRenderMode,
                                      mozilla::wr::FontRenderMode::Mono,
                                      mozilla::wr::FontRenderMode::Sentinel> {};

template <>
struct ParamTraits<mozilla::wr::ExternalImageKeyPair>
    : public ParamTraits_TiedFields<mozilla::wr::ExternalImageKeyPair> {};

template <>
struct ParamTraits<mozilla::wr::RenderReasons>
    : public ParamTraits_TiedFields<mozilla::wr::RenderReasons> {};

template <>
struct ParamTraits<mozilla::wr::ExternalImageSource>
    : public ContiguousEnumSerializer<mozilla::wr::ExternalImageSource,
                                      mozilla::wr::ExternalImageSource::Unknown,
                                      mozilla::wr::ExternalImageSource::Last> {
};

}  // namespace IPC

#endif  // GFX_WEBRENDERMESSAGEUTILS_H
