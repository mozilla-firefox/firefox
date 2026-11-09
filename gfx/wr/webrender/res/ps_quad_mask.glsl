/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// This shader applies a (rounded) rectangle mask to the content of the framebuffer.

#include ps_quad,ellipse

varying highp vec4 vClipLocalPos;

#ifdef WR_FEATURE_FAST_PATH
flat varying highp vec4 v_clip_radii;
flat varying highp vec2 v_clip_size;
#else
flat varying highp vec4 vClipCenter_Radius_TL;
flat varying highp vec4 vClipCenter_Radius_TR;
flat varying highp vec4 vClipCenter_Radius_BR;
flat varying highp vec4 vClipCenter_Radius_BL;
// We pack 4 vec3 clip planes into 3 vec4 to save a varying slot.
flat varying highp vec4 vClipPlane_A;
flat varying highp vec4 vClipPlane_B;
flat varying highp vec4 vClipPlane_C;

#ifdef WR_FEATURE_SUPERELLIPSE
flat varying highp vec4 vClipShape;
flat varying highp vec4 vClipClampedInset;
#endif

#endif
flat varying highp vec2 vClipMode;

#ifdef WR_VERTEX_SHADER

PER_INSTANCE in ivec4 aClipData;

#define CLIP_SPACE_DEVICE       0
#define CLIP_SPACE_PRIMITIVE    1

struct Clip {
    RectWithEndpoint rect;
#ifdef WR_FEATURE_FAST_PATH
    vec4 radii;
#else
    vec4 radii_top;
    vec4 radii_bottom;

#ifdef WR_FEATURE_SUPERELLIPSE
    vec4 shape;
    vec4 inset;
#endif
#endif
    float mode;
    int space;
};

Clip fetch_clip(int index) {
    Clip clip;

    clip.space = aClipData.z;

#ifdef WR_FEATURE_FAST_PATH
    vec4 texels[3] = fetch_from_gpu_buffer_3f(index);
    clip.rect = RectWithEndpoint(texels[0].xy, texels[0].zw);
    clip.radii = texels[1];
    clip.mode = texels[2].x;
#else
#ifdef WR_FEATURE_SUPERELLIPSE
    vec4 texels[6] = fetch_from_gpu_buffer_6f(index);
#else
    vec4 texels[4] = fetch_from_gpu_buffer_4f(index);
#endif
    clip.rect = RectWithEndpoint(texels[0].xy, texels[0].zw);
    clip.radii_top = texels[1];
    clip.radii_bottom = texels[2];
    clip.mode = texels[3].x;
#ifdef WR_FEATURE_SUPERELLIPSE
    clip.shape = texels[4];
    clip.inset = texels[5];
#endif
#endif

    return clip;
}

#ifdef WR_FEATURE_SUPERELLIPSE
vec4 precalc_corner(vec2 center, vec2 radii, vec2 inset, vec2 clip_sign, float k) {
    if (k == 1.0) {
        // round/ellipse corner, precalc the ellipse parameters
        return vec4(center, inverse_radii_squared(radii));
    } else {
        // superellipse, precalc the superellipse parameters
        if (k < 1.0) {
            vec2 reference_radii = (radii == vec2(0.0)) ? vec2(0.0) : radii + inset;
            vec4 offset_radii = compute_contoured_superellipse(reference_radii, k, inset);
            center += offset_radii.xy * clip_sign;
            radii = offset_radii.zw;
        }
        return vec4(center, inverse_radii(radii));
    }
}
#endif

void pattern_vertex(PrimitiveInfo prim_info) {

    Clip clip = fetch_clip(aClipData.y);
    Transform clip_transform = fetch_transform(aClipData.x);

    vClipLocalPos = clip_transform.m * vec4(prim_info.local_pos, 0.0, 1.0);

#ifndef WR_FEATURE_FAST_PATH
    vTransformBounds = vec4(clip.rect.p0, clip.rect.p1);
#endif

    vClipMode.x = clip.mode;

#ifdef WR_FEATURE_FAST_PATH
    // If the radii are uniform, we can use a simpler 2d signed distance
    // function to get a rounded rect clip.
    vec2 half_size = 0.5 * (clip.rect.p1 - clip.rect.p0);
    // Center the position in the box.
    vClipLocalPos.xy -= (half_size + clip.rect.p0) * vClipLocalPos.w;
    v_clip_size = half_size;
    v_clip_radii = clip.radii;
#else
    vec2 r_tl = clip.radii_top.xy;
    vec2 r_tr = clip.radii_top.zw;
    vec2 r_br = clip.radii_bottom.zw;
    vec2 r_bl = clip.radii_bottom.xy;

#ifdef WR_FEATURE_SUPERELLIPSE
    vClipCenter_Radius_TL = precalc_corner(clip.rect.p0 + r_tl,
                                           r_tl,
                                           clip.inset.wx,
                                           vec2(-1.0, -1.0),
                                           clip.shape.x);

    vClipCenter_Radius_TR = precalc_corner(vec2(clip.rect.p1.x - r_tr.x,
                                                clip.rect.p0.y + r_tr.y),
                                           r_tr,
                                           clip.inset.yx,
                                           vec2(1.0, -1.0),
                                           clip.shape.y);

    vClipCenter_Radius_BR = precalc_corner(clip.rect.p1 - r_br,
                                           r_br,
                                           clip.inset.yz,
                                           vec2(1.0, 1.0),
                                           clip.shape.z);

    vClipCenter_Radius_BL = precalc_corner(vec2(clip.rect.p0.x + r_bl.x,
                                                clip.rect.p1.y - r_bl.y),
                                           r_bl,
                                           clip.inset.wz,
                                           vec2(-1.0, 1.0),
                                           clip.shape.w);
#else
    vClipCenter_Radius_TL = vec4(clip.rect.p0 + r_tl,
                                 inverse_radii_squared(r_tl));

    vClipCenter_Radius_TR = vec4(clip.rect.p1.x - r_tr.x,
                                 clip.rect.p0.y + r_tr.y,
                                 inverse_radii_squared(r_tr));

    vClipCenter_Radius_BR = vec4(clip.rect.p1 - r_br,
                                 inverse_radii_squared(r_br));

    vClipCenter_Radius_BL = vec4(clip.rect.p0.x + r_bl.x,
                                 clip.rect.p1.y - r_bl.y,
                                 inverse_radii_squared(r_bl));
#endif

    // We need to know the half-spaces of the corners separate from the center
    // and radius. We compute a point that falls on the diagonal (which is just
    // an inner vertex pushed out along one axis, but not on both) to get the
    // plane offset of the half-space. We also compute the direction vector of
    // the half-space, which is a perpendicular vertex (-y,x) of the vector of
    // the diagonal. We leave the scales of the vectors unchanged.
    vec2 n_tl = -r_tl.yx;
    vec2 n_tr = vec2(r_tr.y, -r_tr.x);
    vec2 n_br = r_br.yx;
    vec2 n_bl = vec2(-r_bl.y, r_bl.x);
    vec3 tl = vec3(n_tl,
                   dot(n_tl, vec2(clip.rect.p0.x, clip.rect.p0.y + r_tl.y)));
    vec3 tr = vec3(n_tr,
                   dot(n_tr, vec2(clip.rect.p1.x - r_tr.x, clip.rect.p0.y)));
    vec3 br = vec3(n_br,
                   dot(n_br, vec2(clip.rect.p1.x, clip.rect.p1.y - r_br.y)));
    vec3 bl = vec3(n_bl,
                   dot(n_bl, vec2(clip.rect.p0.x + r_bl.x, clip.rect.p1.y)));

    vClipPlane_A = vec4(tl.x, tl.y, tl.z, tr.x);
    vClipPlane_B = vec4(tr.y, tr.z, br.x, br.y);
    vClipPlane_C = vec4(br.z, bl.x, bl.y, bl.z);

#ifdef WR_FEATURE_SUPERELLIPSE
    vClipShape = clip.shape;
    vClipClampedInset = min(clip.inset, 0.0);
#endif
#endif

}
#endif

#ifdef WR_FRAGMENT_SHADER

#ifdef WR_FEATURE_FAST_PATH
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

vec4 pattern_fragment(vec4 _base_color) {
    vec2 clip_local_pos = vClipLocalPos.xy / vClipLocalPos.w;
    float aa_range = compute_aa_range(clip_local_pos);

#ifdef WR_FEATURE_FAST_PATH
    float dist = sd_round_box(clip_local_pos, v_clip_size, v_clip_radii);
#else
    vec3 plane_tl = vec3(vClipPlane_A.x, vClipPlane_A.y, vClipPlane_A.z);
    vec3 plane_tr = vec3(vClipPlane_A.w, vClipPlane_B.x, vClipPlane_B.y);
    vec3 plane_br = vec3(vClipPlane_B.z, vClipPlane_B.w, vClipPlane_C.x);
    vec3 plane_bl = vec3(vClipPlane_C.y, vClipPlane_C.z, vClipPlane_C.w);

    float dist;
#ifdef WR_FEATURE_SUPERELLIPSE
    if (vClipShape == vec4(1.0)) {
#endif
        dist = distance_to_rounded_rect(
            clip_local_pos,
            plane_tl,
            vClipCenter_Radius_TL,
            plane_tr,
            vClipCenter_Radius_TR,
            plane_br,
            vClipCenter_Radius_BR,
            plane_bl,
            vClipCenter_Radius_BL,
            vTransformBounds
        );
#ifdef WR_FEATURE_SUPERELLIPSE
    } else {
        dist = distance_to_shaped_rect(
            clip_local_pos,
            vClipCenter_Radius_TL,
            vClipCenter_Radius_TR,
            vClipCenter_Radius_BR,
            vClipCenter_Radius_BL,
            vTransformBounds,
            vClipShape,
            vClipClampedInset
        );
    }
#endif
#endif

    // Compute AA for the given dist and range.
    float alpha = distance_aa(aa_range, dist);

    // Select alpha or inverse alpha depending on clip in/out.
    float final_alpha = mix(alpha, 1.0 - alpha, vClipMode.x);

    return vec4(final_alpha);
}

#ifdef SWGL_DRAW_SPAN
// Software rasterizer fast path for rendering the (rounded) rectangle mask into
// an R8 alpha target (the clip-mask use of this shader). This mirrors the span
// rasterizer that used to live in cs_clip_rectangle.glsl: it splits the span
// into fully transparent, fully opaque and anti-aliased corner runs so the
// ellipse segments and AA are only evaluated where actually needed. When the
// span shader bails out early (perspective, superellipse corners, ...) SWGL
// falls back to the regular fragment shader. For RGBA8 targets (masking already
// rendered content) no R8 span is provided, so that path uses the fragment
// shader as before.
void swgl_drawSpanR8() {
    // Perspective is not supported.
    if (swgl_interpStep(vClipLocalPos).w != 0.0) {
        return;
    }

    // If the span is completely outside the Z-range and clipped out, just
    // output clear so we don't need to consider invalid W in the rest of the
    // shader.
    float w = swgl_forceScalar(vClipLocalPos.w);
    if (w <= 0.0) {
        swgl_commitSolidR8(0.0);
        return;
    }

    w = 1.0 / w;
    vec2 local_pos = vClipLocalPos.xy * w;
    vec2 local_pos0 = swgl_forceScalar(local_pos);
    vec2 local_step = swgl_interpStep(vClipLocalPos).xy * w;
    float step_scale = max(dot(local_step, local_step), 1.0e-6);

    float aa_range = compute_aa_range(local_pos);
    float aa_margin = inversesqrt(aa_range * aa_range * step_scale);

    #ifdef WR_FEATURE_FAST_PATH
        vec4 clip_rect = vec4(-v_clip_size, v_clip_size);
    #else
        vec4 clip_rect = vTransformBounds;
    #endif

    vec4 clip_dist =
        mix(clip_rect, clip_rect.zwxy, lessThan(local_step, vec2(0.0)).xyxy)
            - local_pos0.xyxy;

    // Half-width, in local units, of the band around an edge over which
    // distance_aa() produces partial coverage.
    float aa_band = 0.5 * recip(aa_range);
    // An axis the span does not step along has the same distance to its two
    // edges for every pixel of the span, so the inside/outside test below
    // cannot resolve an edge that runs parallel to the span and coverage ends
    // up quantized to whole spans. Grow the rect by the AA band so that a span
    // landing inside the band counts as inside, which leaves the corner
    // selection below unchanged, and remember that the span is only partially
    // covered so the opaque run can be dropped further down.
    // `inset` is the distance from the span to the nearer of an axis' two
    // edges, positive when the span is between them.
    vec2 inset = min(-clip_dist.xy, clip_dist.zw);
    bvec2 flat_axis = equal(local_step, vec2(0.0));
    bvec2 partial = lessThan(inset, vec2(aa_band));
    bool aa_span = (flat_axis.x && partial.x) || (flat_axis.y && partial.y);

    clip_dist =
        mix(1.0e6 * step(0.0, clip_dist - vec4(aa_band, aa_band, -aa_band, -aa_band)),
            clip_dist * recip(local_step).xyxy,
            notEqual(local_step, vec2(0.0)).xyxy);

    float opaque_start = max(clip_dist.x, clip_dist.y);
    float opaque_end = min(clip_dist.z, clip_dist.w);
    float aa_start = opaque_start;
    float aa_end = opaque_end;

    vec3 start_plane = vec3(1.0e6);
    vec3 end_plane = vec3(1.0e6);

    // plane is assumed to be a vec3 with normal in (X, Y) and offset in Z.
    #define CLIP_CORNER(plane, info) do {                                     \
        float dist = dot(local_pos0, plane.xy) - plane.z;                     \
        float scale = -dot(local_step, plane.xy);                             \
        if (scale >= 0.0) {                                                   \
            if (dist > opaque_start * scale) {                                \
                SET_CORNER(start_corner, info);                               \
                start_plane = plane;                                          \
                float inv_scale = recip(max(scale, 1.0e-6));                  \
                opaque_start = dist * inv_scale;                              \
                float apex = (0.7071 - 0.5) * 2.0 * abs(plane.x * plane.y);   \
                aa_start = opaque_start - apex * inv_scale;                   \
            }                                                                 \
        } else if (dist > opaque_end * scale) {                               \
            SET_CORNER(end_corner, info);                                     \
            end_plane = plane;                                                \
            float inv_scale = recip(min(scale, -1.0e-6));                     \
            opaque_end = dist * inv_scale;                                    \
            float apex = (0.7071 - 0.5) * 2.0 * abs(plane.x * plane.y);       \
            aa_end = opaque_end - apex * inv_scale;                           \
        }                                                                     \
    } while (false)

    #ifdef WR_FEATURE_FAST_PATH
        #define OFFSET_FOR(radii) \
          (v_clip_size.x + v_clip_size.y - radii) * radii
        vec3 plane_br = vec3(v_clip_radii.xx, OFFSET_FOR(v_clip_radii.x));
        vec3 plane_tr = vec3(v_clip_radii.y, -v_clip_radii.y, OFFSET_FOR(v_clip_radii.y));
        vec3 plane_bl = vec3(-v_clip_radii.z, v_clip_radii.z, OFFSET_FOR(v_clip_radii.z));
        vec3 plane_tl = vec3(-v_clip_radii.ww, OFFSET_FOR(v_clip_radii.w));

        #define SET_CORNER(corner, info)

        CLIP_CORNER(plane_tl, );
        CLIP_CORNER(plane_tr, );
        CLIP_CORNER(plane_br, );
        CLIP_CORNER(plane_bl, );

        #define AA_RECT(local_pos) \
            sd_round_box(local_pos, v_clip_size, v_clip_radii)
    #else
        // The span fast path only handles elliptical corners. For superellipse
        // shapes, bail out and let SWGL fall back to the fragment shader.
        #ifdef WR_FEATURE_SUPERELLIPSE
            return;
        #endif

        // Unpack the corner half-spaces packed into vClipPlane_A/B/C.
        vec3 plane_tl = vec3(vClipPlane_A.x, vClipPlane_A.y, vClipPlane_A.z);
        vec3 plane_tr = vec3(vClipPlane_A.w, vClipPlane_B.x, vClipPlane_B.y);
        vec3 plane_br = vec3(vClipPlane_B.z, vClipPlane_B.w, vClipPlane_C.x);
        vec3 plane_bl = vec3(vClipPlane_C.y, vClipPlane_C.z, vClipPlane_C.w);

        vec4 start_corner = vec4(vec2(1.0e6), vec2(1.0));
        vec4 end_corner = vec4(vec2(1.0e6), vec2(1.0));

        #define SET_CORNER(corner, info) corner = info

        CLIP_CORNER(plane_tl, vClipCenter_Radius_TL);
        CLIP_CORNER(plane_tr, vClipCenter_Radius_TR);
        CLIP_CORNER(plane_br, vClipCenter_Radius_BR);
        CLIP_CORNER(plane_bl, vClipCenter_Radius_BL);

        #define AA_RECT(local_pos) \
            signed_distance_rect(local_pos, vTransformBounds.xy, vTransformBounds.zw)
        #define AA_CORNER(local_pos, corner) \
            distance_to_ellipse_approx(local_pos - corner.xy, corner.zw, 1.0)
    #endif

    aa_margin = max(aa_margin - max(aa_start - aa_end, 0.0), 0.0);
    aa_start -= aa_margin;
    aa_end += aa_margin;

    if (aa_span) {
        // No pixel of the span is fully covered, so collapse the opaque run and
        // let the anti-aliased runs on either side of it cover the whole span.
        opaque_end = min(opaque_end, opaque_start);
    }

    ivec4 steps = ivec4(clamp(
        swgl_SpanLength -
            swgl_StepSize *
                vec4(floor(aa_start), ceil(opaque_start), floor(opaque_end), ceil(aa_end)),
        0.0, swgl_SpanLength));
    int aa_start_len = steps.x;
    int opaque_start_len = steps.y;
    int opaque_end_len = steps.z;
    int aa_end_len = steps.w;

    // Output fully clear while we're outside the AA region.
    if (swgl_SpanLength > aa_start_len) {
        int num_aa = swgl_SpanLength - aa_start_len;
        swgl_commitPartialSolidR8(num_aa, vClipMode.x);
        local_pos += float(num_aa / swgl_StepSize) * local_step;
    }
    #ifdef AA_CORNER
    if (start_plane.x < 1.0e5) {
        while (swgl_SpanLength > opaque_start_len) {
            float alpha = distance_aa(aa_range,
                dot(local_pos, start_plane.xy) > start_plane.z
                    ? AA_CORNER(local_pos, start_corner)
                    : AA_RECT(local_pos));
            swgl_commitColorR8(mix(alpha, 1.0 - alpha, vClipMode.x));
            local_pos += local_step;
        }
    }
    #endif
    // If there's no start corner, just do rect AA until opaque.
    while (swgl_SpanLength > opaque_start_len) {
        float alpha = distance_aa(aa_range, AA_RECT(local_pos));
        swgl_commitColorR8(mix(alpha, 1.0 - alpha, vClipMode.x));
        local_pos += local_step;
    }
    // Now we're finally in the opaque inner octagon part of the span.
    if (swgl_SpanLength > opaque_end_len) {
        int num_opaque = swgl_SpanLength - opaque_end_len;
        swgl_commitPartialSolidR8(num_opaque, 1.0 - vClipMode.x);
        local_pos += float(num_opaque / swgl_StepSize) * local_step;
    }
    #ifdef AA_CORNER
    if (end_plane.x < 1.0e5) {
        while (swgl_SpanLength > aa_end_len) {
            float alpha = distance_aa(aa_range,
                dot(local_pos, end_plane.xy) > end_plane.z
                    ? AA_CORNER(local_pos, end_corner)
                    : AA_RECT(local_pos));
            swgl_commitColorR8(mix(alpha, 1.0 - alpha, vClipMode.x));
            local_pos += local_step;
        }
    }
    #endif
    // If there's no end corner, just do rect AA until clear.
    while (swgl_SpanLength > aa_end_len) {
        float alpha = distance_aa(aa_range, AA_RECT(local_pos));
        swgl_commitColorR8(mix(alpha, 1.0 - alpha, vClipMode.x));
        local_pos += local_step;
    }
    // We're now outside the outer AA octagon on the other side.
    if (swgl_SpanLength > 0) {
        swgl_commitPartialSolidR8(swgl_SpanLength, vClipMode.x);
    }

    #undef CLIP_CORNER
    #undef SET_CORNER
    #undef OFFSET_FOR
    #undef AA_RECT
    #undef AA_CORNER
}
#endif

#endif
