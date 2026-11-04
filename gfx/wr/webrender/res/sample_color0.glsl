/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// This file provides the boilerplate for sampling from sColor0 with strict sample bounds.

#include shared

flat varying highp vec4 v_uv0_sample_bounds;
varying highp vec2 v_uv0;

#ifdef WR_VERTEX_SHADER

/// sample_pos is in 0..1 normalized coordinates
/// uv_rect is in pixel.
void vs_init_sample_color0(vec2 sample_pos, RectWithEndpoint uv_rect) {
    vec2 uv = mix(uv_rect.p0, uv_rect.p1, sample_pos);

    // For sampler2DRect, the uv_rect is already in pixel space and sampling
    // expects pixel-space coordinates, so skip the normalization.
#ifdef WR_FEATURE_TEXTURE_RECT
    vec2 texture_size = vec2(1.0);
#else
    vec2 texture_size = vec2(TEX_SIZE(sColor0));
#endif

    v_uv0 = uv / texture_size;

    // The uv rect may be flipped (for example Y-flipped external images), in which case
    // p0 > p1. Normalize with min/max so the sample bounds stay valid.
    vec2 min_uv = min(uv_rect.p0, uv_rect.p1);
    vec2 max_uv = max(uv_rect.p0, uv_rect.p1);

    v_uv0_sample_bounds = vec4(
        min_uv + vec2(0.5),
        max_uv - vec2(0.5)
    ) / texture_size.xyxy;
}

#endif

#ifdef WR_FRAGMENT_SHADER

/// The vertex shader must have called vs_init_sample_color0
vec4 fs_sample_color0() {
    vec2 uv = clamp(v_uv0, v_uv0_sample_bounds.xy, v_uv0_sample_bounds.zw);
    vec4 texel = TEX_SAMPLE(sColor0, uv);

    return texel;
}

#endif
