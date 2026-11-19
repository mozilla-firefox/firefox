/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// NOTE: No include guard.  This is meant to be included to generate different
// code based on how GFXINFO_FEATURE is defined, possibly multiple times in a
// single translation unit.
//
// GFXINFO_FEATURE(id, pref_name)
// - Use when you need a feature for blocklisting, allowed by default.
//
// GFXINFO_FEATURE_ALLOWLIST(id, pref_name)
// - Use when you need a feature for allowlisting, blocked by default.
//   This allow the use of feature statuses:
//   - FEATURE_ALLOW_ALWAYS
//   - FEATURE_ALLOW_QUALIFIED
//   - FEATURE_DENIED (reserved for internal use)
//
// GFXINFO_FEATURE_RETIRED(id, pref_name)
// - Use when a feature is retired. It should no longer be used. The enum
//   will remain defined to prevent reuse, but it is otherwise not usable
//   in the compiled or downloadable blocklists.
//
// GFXINFO_FEATURE_MISMATCHED(id, blocklist_name, pref_name)
// - Do not use for new entries, this is when the id and the blocklist name
//   did not match in the downloadable blocklist.

/* clang-format off */

/* Wildcard to block all features, starting in 123. */
GFXINFO_FEATURE(ALL, "all")
/* Wildcard to block all optional features, starting in 123. */
GFXINFO_FEATURE(OPTIONAL, "optional")
/* Whether Direct3D 9 is supported for layers, always present. */
GFXINFO_FEATURE(DIRECT3D_9_LAYERS, "layers.direct3d9")
/* Whether Direct3D 10.0 is supported for layers, always present. */
GFXINFO_FEATURE(DIRECT3D_10_LAYERS, "layers.direct3d10")
/* Whether Direct3D 10.1 is supported for layers, always present. */
GFXINFO_FEATURE(DIRECT3D_10_1_LAYERS, "layers.direct3d10-1")
/* Whether OpenGL is supported for layers, always present. */
GFXINFO_FEATURE(OPENGL_LAYERS, "layers.opengl")
/* Whether WebGL is supported via OpenGL, always present. */
GFXINFO_FEATURE(WEBGL_OPENGL, "webgl.opengl")
/* Whether WebGL is supported via ANGLE (D3D9 -- does not check for the presence of ANGLE libs). */
GFXINFO_FEATURE(WEBGL_ANGLE, "webgl.angle")
/* (Unused) Whether WebGL antialiasing is supported. */
GFXINFO_FEATURE_RETIRED(WEBGL_MSAA, "webgl.msaa")
/* (Unused) Whether Stagefright is supported, starting in 17. */
GFXINFO_FEATURE_RETIRED(STAGEFRIGHT, "stagefright")
/* Whether Webrtc Hardware H.264 acceleration is supported, starting in 71. */
GFXINFO_FEATURE(WEBRTC_HW_ACCELERATION_H264, "webrtc.hw.acceleration.h264")
/* Whether Direct3D 11 is supported for layers, starting in 32. */
GFXINFO_FEATURE(DIRECT3D_11_LAYERS, "layers.direct3d11")
/* Whether hardware accelerated video decoding is supported, starting in 36. */
GFXINFO_FEATURE(HARDWARE_VIDEO_DECODING, "hardwarevideodecoding")
/* Whether Vulkan hardware video decoding is supported (Linux). */
GFXINFO_FEATURE(HARDWARE_VIDEO_DECODING_VULKAN, "hardwarevideodecodingvulkan")
/* Whether Direct3D 11 is supported for ANGLE, starting in 38. */
GFXINFO_FEATURE(DIRECT3D_11_ANGLE, "direct3d11angle")
/* Whether Webrtc Hardware acceleration is supported, starting in 42. */
GFXINFO_FEATURE(WEBRTC_HW_ACCELERATION_ENCODE, "webrtc.hw.acceleration.encode")
/* Whether Webrtc Hardware acceleration is supported, starting in 42. */
GFXINFO_FEATURE(WEBRTC_HW_ACCELERATION_DECODE, "webrtc.hw.acceleration.decode")
/* Whether Canvas acceleration is supported, starting in 45 */
GFXINFO_FEATURE(CANVAS2D_ACCELERATION, "canvas2d.acceleration")
/* Whether hardware VP8 decoding is supported, starting in 48; downloadable blocking in 100. */
GFXINFO_FEATURE(VP8_HW_DECODE, "vp8.hw-decode")
/* Whether hardware VP9 decoding is supported, starting in 48; downloadable blocking in 100. */
GFXINFO_FEATURE(VP9_HW_DECODE, "vp9.hw-decode")
/* Whether NV_dx_interop2 is supported, starting in 50; downloadable blocking in 58. */
GFXINFO_FEATURE(DX_INTEROP2, "dx.interop2")
/* Whether the GPU process is supported, starting in 52; downloadable blocking in 58. */
GFXINFO_FEATURE(GPU_PROCESS, "gpu.process")
/* Whether the WebGL2 is supported, starting in 54. */
GFXINFO_FEATURE(WEBGL2, "webgl2")
/* Whether D3D11 keyed mutex is supported, starting in 56. */
GFXINFO_FEATURE(D3D11_KEYED_MUTEX, "d3d11.keyed.mutex")
/* Whether WebRender is supported, starting in 62. */
GFXINFO_FEATURE(WEBRENDER, "webrender")
/* Does D3D11 support NV12 video format, starting in 60. */
GFXINFO_FEATURE(DX_NV12, "dx.nv12")
/* Does D3D11 support P010 video format, starting in 64, downloadable blocking in 133. */
GFXINFO_FEATURE(DX_P010, "dx.p010")
/* Does D3D11 support P016 video format, starting in 64, downloadable blocking in 133. */
GFXINFO_FEATURE(DX_P016, "dx.p016")
/* Whether OpenGL swizzle configuration of texture units is supported, starting in 70. */
GFXINFO_FEATURE(GL_SWIZZLE, "gl.swizzle")
/* Whether WebRender native compositor is supported, starting in 73 */
GFXINFO_FEATURE(WEBRENDER_COMPOSITOR, "webrender.compositor")
/* Whether WebRender can use scissored clears for cached surfaces, staring in 79 */
GFXINFO_FEATURE(WEBRENDER_SCISSORED_CACHE_CLEARS, "webrender.scissored_cache_clears")
/* Support webgl.out-of-process: true (starting in 83) */
GFXINFO_FEATURE_RETIRED(ALLOW_WEBGL_OUT_OF_PROCESS, "webgl.allow-oop")
/* Is OpenGL threadsafe (starting in 83) */
GFXINFO_FEATURE(THREADSAFE_GL, "gl.threadsafe")
/* Whether webrender uses pre-optimized shaders, starting in 87; downloadable blocking in 133. */
GFXINFO_FEATURE(WEBRENDER_OPTIMIZED_SHADERS, "webrender.optimized-shaders")
/* Whether we prefer EGL over GLX with X11, starting in 88. */
GFXINFO_FEATURE(X11_EGL, "x11.egl")
/* Whether DMABUF is supported, starting in 88. */
GFXINFO_FEATURE(DMABUF, "dmabuf")
/* Whether webrender caches shader program binaries to disk, starting in 89; downloadable blocking in 133. */
GFXINFO_FEATURE(WEBRENDER_SHADER_CACHE, "webrender.program-binary-disk")
/* Whether partial present is allowed with WebRender, starting in 98. */
GFXINFO_FEATURE(WEBRENDER_PARTIAL_PRESENT, "webrender.partial-present")
/* Whether WebGPU is supported, starting in 100. */
GFXINFO_FEATURE(WEBGPU, "webgpu")
/* Whether video overlay of hardware decoded video is supported, starting in 100. */
GFXINFO_FEATURE(VIDEO_OVERLAY, "video-overlay")
/* Whether hardware decoded video zero copy is supported, starting in 101. */
GFXINFO_FEATURE_ALLOWLIST(HW_DECODED_VIDEO_ZERO_COPY, "hw-video-zero-copy")
/* Whether DMABUF export is supported, starting in 103; downloadable blocking in 133. */
GFXINFO_FEATURE(DMABUF_SURFACE_EXPORT, "dmabuf.surface-export")
/* Whether reuse decoder device is supported, starting in 104. */
GFXINFO_FEATURE(REUSE_DECODER_DEVICE, "reuse-decoder-device")
/* Whether to allow backdrop filter, starting in 105. */
GFXINFO_FEATURE(BACKDROP_FILTER, "backdrop.filter")
/* Whether to use Accelerated Canvas2D, starting in 110. */
GFXINFO_FEATURE(ACCELERATED_CANVAS2D, "accelerated-canvas2d")
/* Whether hardware H264 decoding is supported, starting in 114; downloadable blocking in 132. */
GFXINFO_FEATURE(H264_HW_DECODE, "h264.hw-decode")
/* Whether hardware AV1 decoding is supported, starting in 114; downloadable blocking in 132. */
GFXINFO_FEATURE(AV1_HW_DECODE, "av1.hw-decode")
/* Whether hardware HEVC decoding is supported, starting and downloadable blocking in 137. */
GFXINFO_FEATURE(HEVC_HW_DECODE, "hevc.hw-decode")
/* Whether video overlay of software decoded video is supported, starting in 116; downloadable blocking in 132. */
GFXINFO_FEATURE(VIDEO_SOFTWARE_OVERLAY, "video-software-overlay")
/* Whether WebGL is allowed to use hardware rendering, otherwise software fallbacks, starting in 120 (115esr); downloadable blocking in 132. */
GFXINFO_FEATURE(WEBGL_USE_HARDWARE, "webgl-use-hardware")
/* Whether overlay is allowed to VideoProcessor-HDR on SDR content, starting in 125. */
GFXINFO_FEATURE_MISMATCHED(OVERLAY_VP_AUTO_HDR, FEATURE_OVERLAY_VP_AUTO_HDR, "overlay-vp-auto-hdr")
/* Whether overlay is allowed to VideoProcessor Super Resolution, starting in 125. */
GFXINFO_FEATURE_MISMATCHED(OVERLAY_VP_SUPER_RESOLUTION, FEATURE_OVERLAY_VP_SUPER_RESOLUTION, "overlay-vp-super-resolution")
/* Whether DMABUF is supported when using WebGL, starting in 88. */
GFXINFO_FEATURE(DMABUF_WEBGL, "dmabuf-webgl")
/* Whether DirectComposition is supported for compositing, starting in 141. */
GFXINFO_FEATURE(WEBRENDER_DCOMP, "webrender.dcomp")
/* Whether hardware VP8 encoding is supported, starting in 140. */
GFXINFO_FEATURE(VP8_HW_ENCODE, "vp8.hw-encode")
/* Whether hardware VP9 encoding is supported, starting in 140. */
GFXINFO_FEATURE(VP9_HW_ENCODE, "vp9.hw-encode")
/* Whether hardware H264 encoding is supported, starting in 140. */
GFXINFO_FEATURE(H264_HW_ENCODE, "h264.hw-encode")
/* Whether hardware AV1 encoding is supported, starting in 140. */
GFXINFO_FEATURE(AV1_HW_ENCODE, "av1.hw-encode")
/* Whether hardware HEVC encoding is supported, starting in 140. */
GFXINFO_FEATURE(HEVC_HW_ENCODE, "hevc.hw-encode")
/* Whether Windows Media Fundation hardware DRM is supported, starting in 141. */
GFXINFO_FEATURE(WMF_HW_DRM, "wmf.hw-drm")
/* Whether OpenGL is allowed to use normalized 16-bit texture formats, starting in 141. */
GFXINFO_FEATURE_MISMATCHED(GL_NORM16_TEXTURES, GL_NORM16, "gl.norm16-textures")
/* Whether hardware accelerated video encoding is supported, starting in 141. */
GFXINFO_FEATURE(HARDWARE_VIDEO_ENCODING, "hardwarevideoencoding")
/* Whether WebGPU's GPUExternalTexture API is supported, starting in 143. */
GFXINFO_FEATURE(WEBGPU_EXTERNAL_TEXTURE, "webgpu.external-texture")
/* Whether Mesa threading is allowed, starting in 146. */
GFXINFO_FEATURE(MESA_THREADING, "mesa.threading")
/* Whether WebGL is allowed, starting in 150. */
GFXINFO_FEATURE(WEBGL, "webgl")
/* Whether high dynamic range video is reported in media capabilities, starting in 152. */
GFXINFO_FEATURE(VIDEO_HDR, "video-hdr")
/* Whether WebGL is supported via ANGLE's Metal backend, starting in 154. */
GFXINFO_FEATURE(WEBGL_ANGLE_METAL, "webgl.angle-metal")
/* Whether webrender is supported on ANGLE's Metal backend, starting in 154. */
GFXINFO_FEATURE(WEBRENDER_ANGLE_METAL, "webrender.angle-metal")
/* Whether WebRender is supported via ANGLE's OpenGL/OpenGL ES backend. */
GFXINFO_FEATURE(WEBRENDER_ANGLE_GL, "webrender.angle-gl")
/* Whether YUV DMA-BUFs can be passed directly to the Wayland compositor. */
GFXINFO_FEATURE(DMABUF_YUV_DIRECT_COMPOSITION, "dmabuf-yuv-direct-composition")
