/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// This shader repeats an image pattern.

#include ps_quad

varying highp vec2 v_uv;
flat varying highp vec4 v_uv_sample_bounds;
flat varying highp vec4 v_uv_bounds;
flat varying highp vec4 v_repetitions_and_spacing_threshold;
#define v_repetitions v_repetitions_and_spacing_threshold.xy
#define v_spacing_threshold v_repetitions_and_spacing_threshold.zw

#ifdef WR_VERTEX_SHADER

struct RepeatedPattern {
    // In layout space.
    vec2 stretch_size;
    // In layout space.
    vec2 spacing;
};

RepeatedPattern fetch_repated_pattern(int address) {
    vec4 payload = fetch_from_gpu_buffer_1f(address);
    return RepeatedPattern(payload.xy, payload.zw);
}

void pattern_vertex(PrimitiveInfo info) {
    RepeatedPattern pattern = fetch_repated_pattern(info.pattern_input.x);
    vec2 f = (info.local_pos - info.pattern_rect.p0) / rect_size(info.pattern_rect);

    // In layout coordinates.
    vec2 inv_repeated_size = vec2(1.0) / (pattern.stretch_size + pattern.spacing);

    // Number of repetitions on the x and y axis.
    vec2 repeat = rect_size(info.pattern_rect) * inv_repeated_size;
    v_repetitions = repeat;

    // Normalized relative to the source uv bounds.
    v_spacing_threshold = pattern.stretch_size * inv_repeated_size;

    // In pixels.
    vec2 uv0 = info.segment.uv_rect.p0;
    vec2 uv1 = info.segment.uv_rect.p1;
    vec2 min_uv = min(uv0, uv1);
    vec2 max_uv = max(uv0, uv1);

    // In source texels.
    vec2 uv_px = (mix(uv0, uv1, f) - min_uv) * repeat.xy;

    // Normalized relative to the source uv bounds.
    v_uv = uv_px / (max_uv - min_uv);

    vec2 inv_texture_size = vec2(1.0) / vec2(TEX_SIZE(sColor0));

    // In normalized texel coordinates.
    v_uv_sample_bounds = vec4(
        min_uv + vec2(0.5),
        max_uv - vec2(0.5)
    ) * inv_texture_size.xyxy;
    v_uv_bounds = vec4(min_uv, max_uv) * inv_texture_size.xyxy;
}

#endif

#ifdef WR_FRAGMENT_SHADER

vec4 pattern_fragment(vec4 color) {
    // In normalized pixel coordinates.
    vec2 uv_size = v_uv_bounds.zw - v_uv_bounds.xy;

    // This prevents the uv on the top and left parts of the primitive that was inflated
    // for anti-aliasing purposes from going beyound the range covered by the regular
    // (non-inflated) primitive.
    vec2 local_uv = max(v_uv, vec2(0.0));

    // Normalized relative to the source uv bounds.
    vec2 repeated_uv = fract(local_uv);

    if (repeated_uv.x > v_spacing_threshold.x || repeated_uv.y > v_spacing_threshold.y) {
        // Make pixels transparent in the spacing area.
        color *= 0.0;
    }

    // At this point repeated_uv does a linear ramp over the whole span of each repetition,
    // which includes the spacing area. It is equal to zero at the beginning of the repetition
    // and one at the end of the spacing area just before the next repetition.
    // What we need is for the the ramp to be equal to one just before the spacing area.
    // This is what dividing by the spacing threshold achives.
    repeated_uv /= v_spacing_threshold;

    // Now express repeated_uv in pixels and clamp it to the source's sample bounds.
    repeated_uv = repeated_uv * uv_size + v_uv_bounds.xy;
    repeated_uv = max(repeated_uv, v_uv_sample_bounds.xy);
    repeated_uv = min(repeated_uv, v_uv_sample_bounds.zw);

    // This takes care of the bottom and right inflated parts.
    // We do it after the modulo because the latter wraps around the values exactly on
    // the right and bottom edges, which we do not want.
    if (local_uv.x >= v_repetitions.x) {
        repeated_uv.x = v_uv_bounds.z;
    }
    if (local_uv.y >= v_repetitions.y) {
        repeated_uv.y = v_uv_bounds.w;
    }

    vec4 texel = TEX_SAMPLE(sColor0, repeated_uv);

    return color * texel;
}

#if defined(SWGL_DRAW_SPAN)
void swgl_drawSpanRGBA8() {
    if (!swgl_isTextureRGBA8(sColor0)) {
        return;
    }

    if (v_spacing_threshold.x < 1.0 || v_spacing_threshold.y < 1.0) {
        // TODO: SWGL's repeating span shaders don't support spacing.
        return;
    }

    if (v_color != vec4(1.0)) {
        swgl_commitTextureRepeatColorRGBA8(sColor0, v_uv, v_repetitions, v_uv_bounds, v_uv_sample_bounds, v_color);
    } else {
        swgl_commitTextureRepeatRGBA8(sColor0, v_uv, v_repetitions, v_uv_bounds, v_uv_sample_bounds);
    }
}
#endif

#endif
