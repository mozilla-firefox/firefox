/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Composite a picture cache tile into the framebuffer.

// This shader must remain compatible with ESSL 1, at least for the
// WR_FEATURE_TEXTURE_EXTERNAL_ESSL1 feature, so that it can be used to render
// video on GLES devices without GL_OES_EGL_image_external_essl3 support.
// This means we cannot use textureSize(), int inputs/outputs, etc.

#include shared

#ifdef WR_FEATURE_YUV
#include yuv
#endif

#ifndef WR_FEATURE_FAST_PATH
// Parameters for compositor clip
varying highp vec2 vNormalizedDevicePos;
flat varying highp vec2 vRoundedClipParams;
flat varying highp vec4 vRoundedClipRadii;
#endif

#ifdef WR_FEATURE_YUV
flat varying YUV_PRECISION vec3 vYcbcrBias;
flat varying YUV_PRECISION mat3 vRgbFromDebiasedYcbcr;
// YUV format. Packed in to vector to avoid bug 1630356.
flat varying mediump ivec2 vYuvFormat;

#ifdef SWGL_DRAW_SPAN
flat varying mediump int vRescaleFactor;
#endif
varying highp vec2 vUV_y;
varying highp vec2 vUV_u;
varying highp vec2 vUV_v;
flat varying highp vec4 vUVBounds_y;
flat varying highp vec4 vUVBounds_u;
flat varying highp vec4 vUVBounds_v;
#else
varying highp vec2 vUv;
#ifndef WR_FEATURE_FAST_PATH
flat varying mediump vec4 vColor;
flat varying highp vec4 vUVBounds;
#endif
#ifdef WR_FEATURE_TEXTURE_EXTERNAL_ESSL1
uniform mediump vec2 uTextureSize;
#endif
#endif

#ifdef WR_VERTEX_SHADER
// CPU side data is in CompositeInstance (gpu_types.rs) and is
// converted to GPU data using desc::COMPOSITE (renderer/vertex.rs) by
// filling vaos.composite_vao with VertexArrayKind::Composite.
PER_INSTANCE attribute vec4 aDeviceRect;
PER_INSTANCE attribute vec4 aDeviceClipRect;
PER_INSTANCE attribute vec4 aColor;
PER_INSTANCE attribute vec4 aParams;
PER_INSTANCE attribute vec2 aFlip;

#ifndef WR_FEATURE_FAST_PATH
PER_INSTANCE attribute vec4 aDeviceRoundedClipRect;
PER_INSTANCE attribute vec4 aDeviceRoundedClipRadii;
#endif

#ifdef WR_FEATURE_YUV
// YUV treats these as a UV clip rect (clamp)
PER_INSTANCE attribute vec4 aUvRect0;
PER_INSTANCE attribute vec4 aUvRect1;
PER_INSTANCE attribute vec4 aUvRect2;
#else
PER_INSTANCE attribute vec4 aUvRect0;
#endif

#ifdef WR_FEATURE_YUV
YuvPrimitive fetch_yuv_primitive() {
    // From ExternalSurfaceDependency::Yuv:
    int color_space = int(aParams.y);
    int yuv_format = int(aParams.z);
    int channel_bit_depth = int(aParams.w);
    return YuvPrimitive(channel_bit_depth, color_space, yuv_format);
}
#endif

void main(void) {
    // Flip device rect if required
    vec4 device_rect = mix(aDeviceRect.xyzw, aDeviceRect.zwxy, aFlip.xyxy);

    // Get device position
    vec2 device_pos = mix(device_rect.xy, device_rect.zw, aPosition.xy);

    // Clip the position to the device space clip rect
    vec2 clipped_device_pos = clamp(device_pos, aDeviceClipRect.xy, aDeviceClipRect.zw);

#ifndef WR_FEATURE_FAST_PATH
    vec2 half_clip_box_size = 0.5 * (aDeviceRoundedClipRect.zw - aDeviceRoundedClipRect.xy);
    vNormalizedDevicePos = aDeviceRoundedClipRect.xy + half_clip_box_size - clipped_device_pos;
    vRoundedClipParams = half_clip_box_size;
    vRoundedClipRadii = aDeviceRoundedClipRadii;
#endif

    // Derive the normalized UV from the clipped vertex position
    vec2 uv = (clipped_device_pos - device_rect.xy) / (device_rect.zw - device_rect.xy);

#ifdef WR_FEATURE_YUV
    YuvPrimitive prim = fetch_yuv_primitive();

#ifdef SWGL_DRAW_SPAN
    // swgl_commitTextureLinearYUV needs to know the color space specifier and
    // also needs to know how many bits of scaling are required to normalize
    // HDR textures. Note that MSB HDR formats don't need renormalization.
    vRescaleFactor = 0;
    if (prim.channel_bit_depth > 8 && !yuv_format_is_msb_aligned(prim.yuv_format)) {
        vRescaleFactor = 16 - prim.channel_bit_depth;
    }
#endif

    YuvColorMatrixInfo mat_info = get_rgb_from_ycbcr_info(prim);
    vYcbcrBias = mat_info.ycbcr_bias;
    vRgbFromDebiasedYcbcr = mat_info.rgb_from_debiased_ycbrc;

    vYuvFormat.x = prim.yuv_format;

    write_uv_rect(
        aUvRect0.xy,
        aUvRect0.zw,
        uv,
        TEX_SIZE_YUV(sColor0),
        vUV_y,
        vUVBounds_y
    );
    write_uv_rect(
        aUvRect1.xy,
        aUvRect1.zw,
        uv,
        TEX_SIZE_YUV(sColor1),
        vUV_u,
        vUVBounds_u
    );
    write_uv_rect(
        aUvRect2.xy,
        aUvRect2.zw,
        uv,
        TEX_SIZE_YUV(sColor2),
        vUV_v,
        vUVBounds_v
    );
#else
    uv = mix(aUvRect0.xy, aUvRect0.zw, uv);
    // The uvs may be inverted, so use the min and max for the bounds
    vec4 uvBounds = vec4(min(aUvRect0.xy, aUvRect0.zw), max(aUvRect0.xy, aUvRect0.zw));
    if (int(aParams.y) == UV_TYPE_UNNORMALIZED) {
        // using an atlas, so UVs are in pixels, and need to be
        // normalized and clamped.
#if defined(WR_FEATURE_TEXTURE_RECT)
        vec2 texture_size = vec2(1.0, 1.0);
#elif defined(WR_FEATURE_TEXTURE_EXTERNAL_ESSL1)
        vec2 texture_size = uTextureSize;
#else
        vec2 texture_size = vec2(TEX_SIZE(sColor0));
#endif
        uvBounds += vec4(0.5, 0.5, -0.5, -0.5);
    #ifndef WR_FEATURE_TEXTURE_RECT
        uv /= texture_size;
        uvBounds /= texture_size.xyxy;
    #endif
    }

    vUv = uv;
#ifndef WR_FEATURE_FAST_PATH
    vUVBounds = uvBounds;
    // Pass through color
    vColor = aColor;
#endif
#endif

    gl_Position = uTransform * vec4(clipped_device_pos, 0.0, 1.0);
}
#endif

#ifdef WR_FRAGMENT_SHADER

#ifndef WR_FEATURE_FAST_PATH
// See https://www.shadertoy.com/view/4llXD7
// Notes:
//  * pos is centered in the origin (so 0,0 is the center of the box).
//  * The border radii must not be larger than half_box_size.
float sd_round_box(in vec2 pos, in vec2 half_box_size, in vec4 radii) {
    radii.xy = (pos.x > 0.0) ? radii.xy : radii.zw;
    radii.x  = (pos.y > 0.0) ? radii.x  : radii.y;
    vec2 q = abs(pos) - half_box_size + radii.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radii.x;
}
#endif

void main(void) {
#ifdef WR_FEATURE_YUV
    vec4 color = sample_yuv(
        vYuvFormat.x,
        vYcbcrBias,
        vRgbFromDebiasedYcbcr,
        vUV_y,
        vUV_u,
        vUV_v,
        vUVBounds_y,
        vUVBounds_u,
        vUVBounds_v
    );
#else
    // The color is just the texture sample modulated by a supplied color.
    // In the fast path we avoid clamping the UV coordinates and modulating by the color.
#ifdef WR_FEATURE_FAST_PATH
    vec2 uv = vUv;
#else
    vec2 uv = clamp(vUv, vUVBounds.xy, vUVBounds.zw);
#endif
    vec4 texel = TEX_SAMPLE(sColor0, uv);
#ifdef WR_FEATURE_FAST_PATH
    vec4 color = texel;
#else
    vec4 color = vColor * texel;
#endif
#endif

// TODO(gw): Do we need to support this on ESSL1?
#ifndef WR_FEATURE_TEXTURE_EXTERNAL_ESSL1
#ifndef WR_FEATURE_FAST_PATH
    // Apply compositor clip
    float aa_range = compute_aa_range(vNormalizedDevicePos);

    float dist = sd_round_box(
        vNormalizedDevicePos,
        vRoundedClipParams,
        vRoundedClipRadii
    );

    // Compute AA for the given dist and range.
    float clip_alpha =  distance_aa(aa_range, dist);

    // Apply clip alpha
    color *= clip_alpha;
#endif
#endif

    write_output(color);
}

#ifdef SWGL_DRAW_SPAN
void swgl_drawSpanRGBA8() {

#ifndef WR_FEATURE_FAST_PATH
    // If there is per-fragment clipping to do, we need to bail
    // out of the span shader.
    if (any(greaterThan(vRoundedClipRadii, vec4(0.0)))) {
        return;
    }
#endif      // WR_FEATURE_FAST_PATH

#ifdef WR_FEATURE_YUV
    if (vYuvFormat.x == YUV_FORMAT_PLANAR) {
        swgl_commitTextureLinearYUV(sColor0, vUV_y, vUVBounds_y,
                                    sColor1, vUV_u, vUVBounds_u,
                                    sColor2, vUV_v, vUVBounds_v,
                                    vYcbcrBias,
                                    vRgbFromDebiasedYcbcr,
                                    vRescaleFactor);
    } else if (yuv_format_is_biplanar(vYuvFormat.x)) {
        swgl_commitTextureLinearYUV(sColor0, vUV_y, vUVBounds_y,
                                    sColor1, vUV_u, vUVBounds_u,
                                    vYcbcrBias,
                                    vRgbFromDebiasedYcbcr,
                                    vRescaleFactor);
    } else if (vYuvFormat.x == YUV_FORMAT_INTERLEAVED) {
        swgl_commitTextureLinearYUV(sColor0, vUV_y, vUVBounds_y,
                                    vYcbcrBias,
                                    vRgbFromDebiasedYcbcr,
                                    vRescaleFactor);
    }
#else
#ifdef WR_FEATURE_FAST_PATH
    vec4 color = vec4(1.0);
#ifdef WR_FEATURE_TEXTURE_RECT
    vec4 uvBounds = vec4(vec2(0.0), vec2(textureSize(sColor0)));
#else
    vec4 uvBounds = vec4(0.0, 0.0, 1.0, 1.0);
#endif
#else
    vec4 color = vColor;
    vec4 uvBounds = vUVBounds;
#endif

    if (color != vec4(1.0)) {
        swgl_commitTextureColorRGBA8(sColor0, vUv, uvBounds, color, SWGL_NO_CLIP_RECT);
    } else {
        swgl_commitTextureRGBA8(sColor0, vUv, uvBounds, SWGL_NO_CLIP_RECT);
    }
#endif
}
#endif      // SWGL_DRAW_SPAN

#endif
