/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This shader must remain compatible with ESSL 1, at least for the
// WR_FEATURE_TEXTURE_EXTERNAL_ESSL1 feature, so that it can be used to render
// video on GLES devices without GL_OES_EGL_image_external_essl3 support.
// This means we cannot use textureSize(), int inputs/outputs, etc.

#include shared

varying highp vec2 vUv;
flat varying highp vec4 vUvRect;
#ifdef WR_FEATURE_TEXTURE_EXTERNAL_ESSL1
uniform vec2 uTextureSize;
#endif

#ifdef WR_VERTEX_SHADER

PER_INSTANCE attribute vec4 aScaleTargetRect;
PER_INSTANCE attribute vec4 aScaleSourceRect;
PER_INSTANCE attribute float aSourceRectType;

void main(void) {
    vec2 src_offset = aScaleSourceRect.xy;
    vec2 src_size = aScaleSourceRect.zw - aScaleSourceRect.xy;

    // The uvs may be inverted, so use the min and max for the bounds
    vUvRect = vec4(min(aScaleSourceRect.xy, aScaleSourceRect.zw),
                   max(aScaleSourceRect.xy, aScaleSourceRect.zw));
    vUv = (src_offset + src_size * aPosition.xy);

    if (int(aSourceRectType) == UV_TYPE_UNNORMALIZED) {
        vUvRect = vec4(vUvRect.xy + vec2(0.5), vUvRect.zw - vec2(0.5));

#ifdef WR_FEATURE_TEXTURE_RECT
        // In WR_FEATURE_TEXTURE_RECT mode the UV coordinates used to sample
        // from the texture should be unnormalized, so we leave them as is.
        vec2 texture_size = vec2(1, 1);
#elif defined(WR_FEATURE_TEXTURE_EXTERNAL_ESSL1)
        vec2 texture_size = uTextureSize;
#else
        vec2 texture_size = vec2(TEX_SIZE(sColor0));
#endif
        vUvRect /= texture_size.xyxy;
        vUv /= texture_size;
    }

    vec2 pos = mix(aScaleTargetRect.xy, aScaleTargetRect.zw, aPosition.xy);
    gl_Position = uTransform * vec4(pos, 0.0, 1.0);
}

#endif

#ifdef WR_FRAGMENT_SHADER

void main(void) {
    vec2 st = clamp(vUv, vUvRect.xy, vUvRect.zw);
    oFragColor = TEX_SAMPLE(sColor0, st);
}

#ifdef SWGL_DRAW_SPAN
void swgl_drawSpanRGBA8() {
    swgl_commitTextureLinearRGBA8(sColor0, vUv, vUvRect, SWGL_NO_CLIP_RECT);
}
#endif

#endif
