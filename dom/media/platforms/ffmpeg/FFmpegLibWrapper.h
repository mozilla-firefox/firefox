/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FFmpegLibWrapper_h_
#define FFmpegLibWrapper_h_

// The highest libavcodec major version we support. When bumping this for a new
// FFmpeg release, the AV_FUNC_NN bindings, case NN dispatch, and the ffmpegNN/
// subdirectory in FFmpegLibWrapper.cpp / FFmpegRuntimeLinker.cpp must all be
// updated. Static asserts in FFmpegLibWrapper::Link() enforce consistency.
#define FFMPEG_MAX_MAJOR_VERSION 63
#define FFMPEG_MAX_MAJOR_VERSION_STR_HELPER(x) #x
#define FFMPEG_MAX_MAJOR_VERSION_STR(x) FFMPEG_MAX_MAJOR_VERSION_STR_HELPER(x)

#include "ffvpx/tx.h"
#include "mozilla/Attributes.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/Preferences.h"

struct AVCodec;
struct AVCodecContext;
struct AVCodecDescriptor;
struct AVFrame;
struct AVPacket;
struct AVDictionary;
struct AVCodecParserContext;
struct PRLibrary;
struct AVChannelLayout;
struct AVCodecHWConfig;
#ifdef MOZ_WIDGET_GTK
struct AVVAAPIHWConfig;
struct AVHWFramesConstraints;
#endif
struct AVBufferRef;
#ifdef MOZ_WIDGET_ANDROID
typedef struct MediaCodecBuffer AVMediaCodecBuffer;
#endif

namespace mozilla {

struct MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS FFmpegLibWrapper {
  // The class is used only in static storage and so is zero initialized.
  FFmpegLibWrapper() = default;
  // The libraries are not unloaded in the destructor, because doing so would
  // require a static constructor to register the static destructor.  As the
  // class is in static storage, the destructor would only run on shutdown
  // anyway.
  ~FFmpegLibWrapper() = default;

  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(
      LinkResult, (Success, NoProvidedLib, NoAVCodecVersion, CannotUseLibAV57,
                   BlockedOldLibAVVersion, UnknownFutureLibAVVersion,
                   UnknownFutureFFMpegVersion, UnknownOlderFFMpegVersion,
                   MissingFFMpegFunction, MissingLibAVFunction));

  // Examine mAVCodecLib and mAVUtilLib, and attempt to resolve
  // all symbols.
  // Upon failure, the entire object will be reset and any attached libraries
  // will be unlinked.
  LinkResult Link();

  // Reset the wrapper and unlink all attached libraries.
  void Unlink();

#ifdef MOZ_WIDGET_GTK
  // Check if libva and libva-drm are available and we can use HW decode.
  bool IsVAAPIAvailable() const;
#endif

  // Helpers for libavcodec/util logging to integrate with MOZ_LOG.
  static int ToLibLogLevel(LogLevel aLevel);
  static LogLevel FromLibLogLevel(int aLevel);
  static void Log(void* aPtr, int aLevel, const char* aFmt, va_list aArgs);
  void UpdateLogLevel();
  static void RegisterCallbackLogLevel(PrefChangedFunc aCallback);

  // indicate the version of libavcodec linked to.
  // 0 indicates that the function wasn't initialized with Link().
  int mVersion;

  // libavcodec
  unsigned (*avcodec_version)();
  int (*av_lockmgr_register)(int (*cb)(void** mutex, int op));
  AVCodecContext* (*avcodec_alloc_context3)(const AVCodec* codec);
  int (*avcodec_close)(AVCodecContext* avctx);
  int (*avcodec_decode_audio4)(AVCodecContext* avctx, AVFrame* frame,
                               int* got_frame_ptr, const AVPacket* avpkt);
  int (*avcodec_decode_video2)(AVCodecContext* avctx, AVFrame* picture,
                               int* got_picture_ptr, const AVPacket* avpkt);
  AVCodec* (*avcodec_find_decoder)(int id);
  AVCodec* (*avcodec_find_decoder_by_name)(const char* name);
  AVCodec* (*avcodec_find_encoder)(int id);
  AVCodec* (*avcodec_find_encoder_by_name)(const char* name);
  void (*avcodec_flush_buffers)(AVCodecContext* avctx);
  int (*avcodec_open2)(AVCodecContext* avctx, const AVCodec* codec,
                       AVDictionary** options);
  void (*avcodec_register_all)();
  void (*av_init_packet)(AVPacket* pkt);
  AVCodecParserContext* (*av_parser_init)(int codec_id);
  void (*av_parser_close)(AVCodecParserContext* s);
  int (*av_parser_parse2)(AVCodecParserContext* s, AVCodecContext* avctx,
                          uint8_t** poutbuf, int* poutbuf_size,
                          const uint8_t* buf, int buf_size, int64_t pts,
                          int64_t dts, int64_t pos);
  AVCodec* (*av_codec_iterate)(void** opaque);
  int (*av_codec_is_decoder)(const AVCodec* codec);
  int (*av_codec_is_encoder)(const AVCodec* codec);
  void (*avcodec_align_dimensions)(AVCodecContext* s, int* width, int* height);
  int (*av_strerror)(int errnum, char* errbuf, size_t errbuf_size);
  AVCodecDescriptor* (*avcodec_descriptor_get)(int id);

  // only used in libavcodec <= 54
  AVFrame* (*avcodec_alloc_frame)();
  void (*avcodec_get_frame_defaults)(AVFrame* pic);

  // libavcodec v54 only
  void (*avcodec_free_frame)(AVFrame** frame);

  // libavcodec >= v55
  int (*avcodec_default_get_buffer2)(AVCodecContext* s, AVFrame* frame,
                                     int flags);

  // libavcodec >= v57
  void (*av_packet_unref)(AVPacket* pkt);
  void (*av_packet_free)(AVPacket** pkt);
  void (*avcodec_free_context)(AVCodecContext** avctx);

  // libavcodec >= 61
  AVPacket* (*av_packet_alloc)();

  // libavcodec v58 and later only
  int (*avcodec_send_packet)(AVCodecContext* avctx, const AVPacket* avpkt);
  int (*avcodec_receive_packet)(AVCodecContext* avctx, AVPacket* avpkt);
  int (*avcodec_send_frame)(AVCodecContext* avctx, const AVFrame* frame);
  int (*avcodec_receive_frame)(AVCodecContext* avctx, AVFrame* frame);

  // libavutil
  void (*av_log_set_callback)(void (*callback)(void*, int, const char*,
                                               va_list));
  void (*av_log_set_level)(int level);
  void* (*av_malloc)(size_t size);
  void* (*av_mallocz)(size_t size);
  void (*av_freep)(void* ptr);
  int (*av_image_check_size)(unsigned int w, unsigned int h, int log_offset,
                             void* log_ctx);
  int (*av_image_get_buffer_size)(int pix_fmt, int width, int height,
                                  int align);
  const char* (*av_get_sample_fmt_name)(int sample_fmt);
  void (*av_channel_layout_default)(AVChannelLayout* ch_layout,
                                    int nb_channels);
  void (*av_channel_layout_from_mask)(AVChannelLayout* ch_layout,
                                      uint64_t mask);
  int (*av_channel_layout_copy)(AVChannelLayout* dst, AVChannelLayout* src);
  int (*av_dict_set)(AVDictionary** pm, const char* key, const char* value,
                     int flags);
  void (*av_dict_free)(AVDictionary** m);
  int (*av_opt_set)(void* obj, const char* name, const char* val,
                    int search_flags);
  int (*av_opt_set_double)(void* obj, const char* name, double val,
                           int search_flags);
  int (*av_opt_set_int)(void* obj, const char* name, int64_t val,
                        int search_flags);

  // libavutil v55 and later only
  AVFrame* (*av_frame_alloc)();
  AVFrame* (*av_frame_clone)(const AVFrame* frame);
  void (*av_frame_free)(AVFrame** frame);
  void (*av_frame_unref)(AVFrame* frame);
  int (*av_frame_get_buffer)(AVFrame* frame, int align);
  int (*av_frame_make_writable)(AVFrame* frame);
  AVBufferRef* (*av_buffer_create)(uint8_t* data, int size,
                                   void (*free)(void* opaque, uint8_t* data),
                                   void* opaque, int flags);

  // libavutil >= v56
  void* (*av_buffer_get_opaque)(const AVBufferRef* buf);

  // libavutil optional
  int (*av_frame_get_colorspace)(const AVFrame* frame);
  int (*av_frame_get_color_range)(const AVFrame* frame);

  // libavcodec > 58
  const AVCodecHWConfig* (*avcodec_get_hw_config)(const AVCodec* codec,
                                                  int index);
  // libavcodec >= 62
  int (*avcodec_get_supported_config)(const AVCodecContext* avctx,
                                      const AVCodec* codec, int config,
                                      unsigned flags, const void** out_configs,
                                      int* out_num_configs);
  // libavutil >= 58
  AVBufferRef* (*av_hwdevice_ctx_alloc)(int);
  int (*av_hwdevice_ctx_init)(AVBufferRef* ref);
  int (*av_hwdevice_ctx_create)(AVBufferRef** device_ctx, int type,
                                const char* device, AVDictionary* opts,
                                int flags);
  AVBufferRef* (*av_hwframe_ctx_alloc)(AVBufferRef* device_ctx);
  int (*av_hwframe_ctx_init)(AVBufferRef* ref);
  int (*avcodec_get_hw_frames_parameters)(AVCodecContext* avctx,
                                          AVBufferRef* device_ref,
                                          int hw_pix_fmt,
                                          AVBufferRef** out_frames_ref);
  int (*av_hwframe_map)(AVFrame* dst, const AVFrame* src, int flags);
  AVBufferRef* (*av_buffer_ref)(AVBufferRef* buf);
  void (*av_buffer_unref)(AVBufferRef** buf);

#ifdef MOZ_WIDGET_GTK
  AVVAAPIHWConfig* (*av_hwdevice_hwconfig_alloc)(AVBufferRef* device_ctx);
  AVHWFramesConstraints* (*av_hwdevice_get_hwframe_constraints)(
      AVBufferRef* ref, const void* hwconfig);
  void (*av_hwframe_constraints_free)(AVHWFramesConstraints** constraints);
  int (*av_hwframe_transfer_get_formats)(AVBufferRef* hwframe_ctx, int dir,
                                         int** formats, int flags);
  int (*av_hwdevice_ctx_create_derived)(AVBufferRef** dst_ctx, int type,
                                        AVBufferRef* src_ctx, int flags);
  const char* (*av_hwdevice_get_type_name)(int type);
  const char* (*avcodec_get_name)(int id);
  char* (*av_get_pix_fmt_string)(char* buf, int buf_size, int pix_fmt);
#endif

#if defined(MOZ_WIDGET_ANDROID)
  int (*av_mediacodec_release_buffer)(AVMediaCodecBuffer*, int);
  int (*moz_avcodec_mediacodec_is_eos)(AVCodecContext*);
#endif

  // Only ever used with ffvpx
  decltype(::av_tx_init)* av_tx_init;
  decltype(::av_tx_uninit)* av_tx_uninit;

  PRLibrary* mAVCodecLib;
  PRLibrary* mAVUtilLib;
};

}  // namespace mozilla

#endif  // FFmpegLibWrapper
