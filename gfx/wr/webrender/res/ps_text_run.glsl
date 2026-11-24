/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include shared,prim_shared,gpu_buffer

flat varying mediump vec4 v_color;
flat varying mediump vec3 v_mask_swizzle;
// Normalized bounds of the source image in the texture.
flat varying highp vec4 v_uv_bounds;

// Interpolated UV coordinates to sample.
varying highp vec2 v_uv;


#if defined(WR_FEATURE_GLYPH_TRANSFORM) && !defined(SWGL_CLIP_DIST)
varying highp vec4 v_uv_clip;
#endif

#ifdef WR_VERTEX_SHADER

#define VECS_PER_TEXT_RUN           1
#define GLYPHS_PER_GPU_BLOCK        2U

#ifdef WR_FEATURE_GLYPH_TRANSFORM
bool rect_inside_rect(RectWithEndpoint little, RectWithEndpoint big) {
    return all(lessThanEqual(vec4(big.p0, little.p1), vec4(little.p0, big.p1)));
}
#endif //WR_FEATURE_GLYPH_TRANSFORM

struct Glyph {
    vec2 offset;
};

Glyph fetch_glyph(int specific_prim_address,
                  int glyph_index) {
    // Two glyphs are packed in each texel in the GPU cache.
    int glyph_address = specific_prim_address +
                        VECS_PER_TEXT_RUN +
                        int(uint(glyph_index) / GLYPHS_PER_GPU_BLOCK);
    vec4 data = fetch_from_gpu_buffer_1f(glyph_address);
    // Select XY or ZW based on glyph index.
    vec2 glyph = mix(data.xy, data.zw,
                     bvec2(uint(glyph_index) % GLYPHS_PER_GPU_BLOCK == 1U));

    return Glyph(glyph);
}

struct GlyphResource {
    vec4 uv_rect;
    vec2 offset;
    float scale;
};

GlyphResource fetch_glyph_resource(int address) {
    vec4 data[2] = fetch_from_gpu_buffer_2f(address);
    return GlyphResource(data[0], data[1].xy, data[1].z);
}

struct TextRun {
    vec4 color;
};

TextRun fetch_text_run(int address) {
    vec4 data = fetch_from_gpu_buffer_1f(address);
    return TextRun(data);
}

void main() {
    Instance instance = decode_instance_attributes();
    PrimitiveHeader ph = fetch_prim_header(instance.prim_header_address);
    Transform transform = fetch_transform(ph.transform_id);
    ClipArea clip_area = fetch_clip_area(instance.clip_address);
    PictureTask task = fetch_picture_task(ph.picture_task_address);

    int glyph_index = instance.segment_index;
    int color_mode = instance.flags & 0xF;
    int subpx_offset_x = (instance.flags >> 4) & 0x3;
    int subpx_offset_y = (instance.flags >> 6) & 0x3;
    int subpx_dir = (instance.flags >> 8) & 0x3;
    int is_packed_glyph = (instance.flags >> 10) & 0x1;
    // Set when the glyph rasterized from an embedded bitmap strike, which
    // ignores the sub-pixel offset the key asked for and lands on the device
    // grid. Such a glyph must round its pen to nearest, or it sits up to
    // 0.875px left of where layout put it and a snapped clip or mask edge
    // shaves its leading column (bug 2056856).
    int is_bitmap_strike = (instance.flags >> 11) & 0x1;

    TextRun text = fetch_text_run(ph.specific_prim_address);

    // Per-glyph pen position from the CPU (`request_resources`): in device mode
    // the exact unsnapped pen in absolute device space, snapped below; in
    // local-raster mode the already-snapped absolute raster-space pen, used as
    // is. No transform is applied to it here.
    Glyph glyph = fetch_glyph(ph.specific_prim_address, glyph_index);

    GlyphResource res = fetch_glyph_resource(instance.resource_address);

    // For multi-variant glyphs, adjust the UV rect to select the correct quarter
    // of the packed texture based on subpixel offset. This must happen before
    // geometry calculations since the glyph rect size depends on the UV rect.
    if (is_packed_glyph != 0) {
        // A mixed transform packs the full 4x4 offset grid, indexed row-major by
        // (offset_y, offset_x); a single-axis run packs 4 variants.
        int variant_index;
        int slot_count;
        if (subpx_dir == SUBPX_DIR_MIXED) {
            variant_index = subpx_offset_y * 4 + subpx_offset_x;
            slot_count = 16;
        } else {
            variant_index = (subpx_dir == SUBPX_DIR_HORIZONTAL) ? subpx_offset_x : subpx_offset_y;
            slot_count = 4;
        }
        float slot_width = (res.uv_rect.z - res.uv_rect.x) / float(slot_count);
        res.uv_rect.x = res.uv_rect.x + float(variant_index) * slot_width;
        res.uv_rect.z = res.uv_rect.x + slot_width;
    }

    // Snap the glyph pen to the device grid. In device mode `glyph.offset` is
    // the CPU's exact unsnapped pen in absolute device space, so this floor
    // operates on a value received verbatim and is bit-exact with what the CPU
    // would have computed. A strike glyph rounds to nearest; everything else
    // keeps the sub-pixel axis's floor bias, which pairs with the quarter-pixel
    // offset baked into the rasterized variant.
    // A rotated / skewed run (SUBPX_DIR_MIXED) carries a quarter-pixel offset on
    // both axes, so it snaps to a quarter-pixel grid instead of rounding each
    // glyph to a whole device pixel (bug 2063377).
    vec2 snap_bias;
    if (is_bitmap_strike != 0) {
        snap_bias = vec2(0.5);
    } else if (subpx_dir == SUBPX_DIR_HORIZONTAL) {
        snap_bias = vec2(0.125, 0.5);
    } else if (subpx_dir == SUBPX_DIR_VERTICAL) {
        snap_bias = vec2(0.5, 0.125);
    } else if (subpx_dir == SUBPX_DIR_MIXED) {
        snap_bias = vec2(0.125);
    } else {
        snap_bias = vec2(0.5);
    }
    vec2 device_pen = floor(glyph.offset + snap_bias);

    float inv_dps = 1.0 / task.device_pixel_scale;

    VertexInfo vi;
    vec2 f;

#ifdef WR_FEATURE_GLYPH_TRANSFORM
    // Device mode, transformed (2D rotated/skewed) glyph. The glyph rect is
    // axis-aligned in device (glyph-raster) space; `glyph.offset` is the
    // device-grid-snapped pen offset. `write_vertex` clamps to the axis-aligned
    // local clip rect, which would shear a rotated quad — so by default build
    // the quad from the local-space AABB of the four mapped corners (clamping an
    // AABB stays clean) and let `v_uv_clip` mask the rotated glyph within it.
    // When the glyph fits entirely inside the clip rect there is nothing to
    // clamp, so use the exact rotated corners to avoid the AABB's overdraw.
    vec2 device_origin = device_pen + res.scale * res.offset;
    vec2 device_size = res.scale * (res.uv_rect.zw - res.uv_rect.xy);

    vec2 c0 = (transform.inv_m * vec4(device_origin * inv_dps, 0.0, 1.0)).xy;
    vec2 c1 = (transform.inv_m * vec4(vec2(device_origin.x + device_size.x, device_origin.y) * inv_dps, 0.0, 1.0)).xy;
    vec2 c2 = (transform.inv_m * vec4(vec2(device_origin.x, device_origin.y + device_size.y) * inv_dps, 0.0, 1.0)).xy;
    vec2 c3 = (transform.inv_m * vec4((device_origin + device_size) * inv_dps, 0.0, 1.0)).xy;
    RectWithEndpoint local_aabb = RectWithEndpoint(min(min(c0, c1), min(c2, c3)), max(max(c0, c1), max(c2, c3)));

    vec2 local_pos = mix(local_aabb.p0, local_aabb.p1, aPosition.xy);
    if (rect_inside_rect(local_aabb, ph.bounds)) {
        vec2 device_corner = mix(device_origin, device_origin + device_size, aPosition.xy);
        local_pos = (transform.inv_m * vec4(device_corner * inv_dps, 0.0, 1.0)).xy;
    }

    vi = write_vertex(local_pos, ph.bounds, ph.z, transform, task);

    // UV fraction within the glyph rect, in device space from the (possibly
    // clip-clamped) vertex, so clipping is handled correctly for rotated glyphs.
    vec2 device_clamped = (transform.m * vec4(vi.local_pos, 0.0, 1.0)).xy * task.device_pixel_scale;
    f = (device_clamped - device_origin) / device_size;
#else
    int raster_mode = ph.user_data.y;
    if (raster_mode == 0) {
        // Device mode, axis-aligned: the device rect maps to an axis-aligned
        // local rect, so the clip clamp is clean — map this vertex's device
        // corner straight to local.
        vec2 device_origin = device_pen + res.scale * res.offset;
        vec2 device_size = res.scale * (res.uv_rect.zw - res.uv_rect.xy);
        vec2 device_corner = mix(device_origin, device_origin + device_size, aPosition.xy);
        vec2 local_pos = (transform.inv_m * vec4(device_corner * inv_dps, 0.0, 1.0)).xy;

        vi = write_vertex(local_pos, ph.bounds, ph.z, transform, task);

        vec2 device_clamped = (transform.m * vec4(vi.local_pos, 0.0, 1.0)).xy * task.device_pixel_scale;
        f = (device_clamped - device_origin) / device_size;
    } else {
        // Local-raster mode: the glyph was rasterized at `raster_scale` with an
        // identity transform. Position and scale it in local space — mapping the
        // raster-space glyph rect to local by `glyph_scale_inv` — and let
        // `write_vertex` apply the (possibly animated / scaling / perspective)
        // transform. No device snapping happens here (it was done in raster space
        // on the CPU) so glyphs don't wiggle under animation. `glyph.offset` is
        // the absolute snapped raster-space position of the glyph pen.
        float raster_scale = float(ph.user_data.x) / 65535.0;
        float glyph_raster_scale = raster_scale * task.device_pixel_scale;
        float glyph_scale_inv = res.scale / glyph_raster_scale;

        vec2 glyph_origin = glyph_scale_inv * (res.offset + glyph.offset / res.scale);
        RectWithEndpoint glyph_rect = RectWithEndpoint(
            glyph_origin,
            glyph_origin + glyph_scale_inv * (res.uv_rect.zw - res.uv_rect.xy)
        );
        vec2 local_pos = mix(glyph_rect.p0, glyph_rect.p1, aPosition.xy);

        vi = write_vertex(local_pos, ph.bounds, ph.z, transform, task);

        f = (vi.local_pos - glyph_rect.p0) / rect_size(glyph_rect);
    }
#endif

#ifdef WR_FEATURE_GLYPH_TRANSFORM
    // For transformed glyphs the local clip rect is axis-aligned but the glyph
    // quad is rotated, so `write_vertex`'s clamp can pull a corner off the glyph.
    // Clip in glyph space instead: discard fragments outside [0,1] of the rect.
    #ifdef SWGL_CLIP_DIST
        gl_ClipDistance[0] = f.x;
        gl_ClipDistance[1] = f.y;
        gl_ClipDistance[2] = 1.0 - f.x;
        gl_ClipDistance[3] = 1.0 - f.y;
    #else
        v_uv_clip = vec4(f, 1.0 - f);
    #endif
#endif

    write_clip(vi.raster_pos, clip_area, task);

    switch (color_mode) {
        case COLOR_MODE_ALPHA:
            v_mask_swizzle = vec3(0.0, 1.0, 1.0);
            v_color = text.color;
            break;
        case COLOR_MODE_BITMAP_SHADOW:
            #ifdef SWGL_BLEND
                swgl_blendDropShadow(text.color);
                v_mask_swizzle = vec3(1.0, 0.0, 0.0);
                v_color = vec4(1.0);
            #else
                v_mask_swizzle = vec3(0.0, 1.0, 0.0);
                v_color = text.color;
            #endif
            break;
        case COLOR_MODE_COLOR_BITMAP:
            v_mask_swizzle = vec3(1.0, 0.0, 0.0);
            v_color = vec4(text.color.a);
            break;
        case COLOR_MODE_SUBPX_DUAL_SOURCE:
            #ifdef SWGL_BLEND
                swgl_blendSubpixelText(text.color);
                v_mask_swizzle = vec3(1.0, 0.0, 0.0);
                v_color = vec4(1.0);
            #else
                v_mask_swizzle = vec3(text.color.a, 0.0, 0.0);
                v_color = text.color;
            #endif
            break;
        default:
            v_mask_swizzle = vec3(0.0, 0.0, 0.0);
            v_color = vec4(1.0);
    }

    vec2 texture_size = vec2(TEX_SIZE(sColor0));
    vec2 st0 = res.uv_rect.xy / texture_size;
    vec2 st1 = res.uv_rect.zw / texture_size;

    v_uv = mix(st0, st1, f);
    v_uv_bounds = (res.uv_rect + vec4(0.5, 0.5, -0.5, -0.5)) / texture_size.xyxy;
}

#endif // WR_VERTEX_SHADER

#ifdef WR_FRAGMENT_SHADER

Fragment text_fs(void) {
    Fragment frag;

    vec2 tc = clamp(v_uv, v_uv_bounds.xy, v_uv_bounds.zw);
    vec4 mask = texture(sColor0, tc);
    // v_mask_swizzle.z != 0 means we are using an R8 texture as alpha,
    // and therefore must swizzle from the r channel to all channels.
    mask = mix(mask, mask.rrrr, bvec4(v_mask_swizzle.z != 0.0));
    #ifndef WR_FEATURE_DUAL_SOURCE_BLENDING
        mask.rgb = mask.rgb * v_mask_swizzle.x + mask.aaa * v_mask_swizzle.y;
    #endif

    #if defined(WR_FEATURE_GLYPH_TRANSFORM) && !defined(SWGL_CLIP_DIST)
        mask *= float(all(greaterThanEqual(v_uv_clip, vec4(0.0))));
    #endif

    frag.color = v_color * mask;

    #if defined(WR_FEATURE_DUAL_SOURCE_BLENDING) && !defined(SWGL_BLEND)
        frag.blend = mask * v_mask_swizzle.x + mask.aaaa * v_mask_swizzle.y;
    #endif

    return frag;
}


void main() {
    Fragment frag = text_fs();

    float clip_mask = do_clip();
    frag.color *= clip_mask;

    #if defined(WR_FEATURE_DEBUG_OVERDRAW)
        oFragColor = WR_DEBUG_OVERDRAW_COLOR;
    #elif defined(WR_FEATURE_DUAL_SOURCE_BLENDING) && !defined(SWGL_BLEND)
        oFragColor = frag.color;
        oFragBlend = frag.blend * clip_mask;
    #else
        write_output(frag.color);
    #endif
}

#if defined(SWGL_DRAW_SPAN) && defined(SWGL_BLEND) && defined(SWGL_CLIP_DIST)
void swgl_drawSpanRGBA8() {
    // Only support simple swizzles for now. More complex swizzles must either
    // be handled by blend overrides or the slow path.
    if (v_mask_swizzle.x != 0.0 && v_mask_swizzle.x != 1.0) {
        return;
    }

    #ifdef WR_FEATURE_DUAL_SOURCE_BLENDING
        swgl_commitTextureLinearRGBA8(sColor0, v_uv, v_uv_bounds, SWGL_NO_CLIP_RECT);
    #else
        if (swgl_isTextureR8(sColor0)) {
            swgl_commitTextureLinearColorR8ToRGBA8(sColor0, v_uv, v_uv_bounds, v_color, SWGL_NO_CLIP_RECT);
        } else {
            swgl_commitTextureLinearColorRGBA8(sColor0, v_uv, v_uv_bounds, v_color, SWGL_NO_CLIP_RECT);
        }
    #endif
}
#endif

#endif // WR_FRAGMENT_SHADER
