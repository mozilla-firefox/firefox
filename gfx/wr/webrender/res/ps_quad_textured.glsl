/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// This shader renders solid colors or simple images in a color or alpha target.

#include ps_quad,sample_color0

#define v_flags_mode v_flags.x

// See constants in src/pattern/mod.rs.
#define SHADER_MODE_COLOR 0
#define SHADER_MODE_TEXTURE 1
// Read only the input texture's alpha channel.
#define SHADER_MODE_TEXTURE_ALPHA 2
#define MAP_TO_PRIMITIVE 0
#define MAP_TO_SEGMENT 1

#ifdef WR_VERTEX_SHADER

void pattern_vertex(PrimitiveInfo info) {
    // Note: Since the uv rect is passed via segments, This shader cannot sample from a
    // texture if no segments are provided
    if (info.pattern_input.x != SHADER_MODE_COLOR) {
        // Textured (or alpha-only)

        RectWithEndpoint pattern_rect = info.pattern_rect;
        if (info.pattern_input.y == MAP_TO_SEGMENT) {
            pattern_rect = info.segment.rect;
        }

        vec2 f = (info.local_pos - pattern_rect.p0) / rect_size(pattern_rect);
        vs_init_sample_color0(f, info.segment.uv_rect);
    }

    v_flags_mode = info.pattern_input.x;
}

#endif

#ifdef WR_FRAGMENT_SHADER

vec4 pattern_fragment(vec4 color) {
    if (v_flags_mode != SHADER_MODE_COLOR) {
        vec4 texel = fs_sample_color0();

        if (v_flags_mode == SHADER_MODE_TEXTURE_ALPHA) {
            texel = vec4(texel.a);
        }

        color *= texel;
    }

    return color;
}

#if defined(SWGL_DRAW_SPAN)
void swgl_drawSpanRGBA8() {
    if (v_flags_mode == SHADER_MODE_COLOR) {
        swgl_commitSolidRGBA8(v_color);
        return;
    }

    if (v_flags_mode == SHADER_MODE_TEXTURE_ALPHA) {
        return;
    }

    if (v_flags_is_mask != 0) {
        // The fragment path broadcasts the output's r channel to all channels
        // (output_color.rrrr in ps_quad.glsl). expand_mask in swgl produces the
        // same broadcast from an R8 sample, so the R8ToRGBA8 commits match the
        // mask semantics. RGBA8 + mask still falls back to the fragment shader
        // since no swgl commit broadcasts a single channel of an RGBA8 sample.
        if (swgl_isTextureR8(sColor0)) {
            if (v_color != vec4(1.0)) {
                swgl_commitTextureLinearColorR8ToRGBA8(sColor0, v_uv0, v_uv0_sample_bounds, v_color);
            } else {
                swgl_commitTextureLinearR8ToRGBA8(sColor0, v_uv0, v_uv0_sample_bounds);
            }
        }

        return;
    }

    if (!swgl_isTextureRGBA8(sColor0)) {
        return;
    }
    if (v_color != vec4(1.0)) {
        swgl_commitTextureColorRGBA8(sColor0, v_uv0, v_uv0_sample_bounds, v_color);
    } else {
        swgl_commitTextureRGBA8(sColor0, v_uv0, v_uv0_sample_bounds);
    }
}
#endif

#endif
