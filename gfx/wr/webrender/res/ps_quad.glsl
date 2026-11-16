/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// The common infrastructure for ps_quad_* shaders.
///
/// # Memory layout
///
/// The diagram below shows the the various pieces of data fectched in the vertex shader:
///
///```ascii
///                                       (int gpu buffer)
///                                       +------------------+
///                                       |Int header (ivec4)|    (float gpu buffer)
///  (instance-step vertex attr)          |                  |    +-----------+
/// +-----------------------------+       | x: transform id  +--> | Transform |
/// |    Quad instance (uvec4)    |  +--> | y: z id          |    +-----------+
/// |                             |  |    | zw: pattern data |
/// | x: int prim address        +---+    +------------------+   (float gpu buffer)
/// | y: float prim address      +--------------------------> +-------------------+--------------+-+-+
/// | z: quad flags               |     (float gpu buffer)    |     Quad Prim     | Quad Segment | | |
/// |    edge flags               |   +--------------------+  |                   |              | | |
/// |    part index               |   |     Picture task   |  | bounds            | rect         | | |
/// |    segment index            |   |                    |  | clip              | uv rect      | | |
/// | w: picture task address    +--> | task rect          |  | uv rect           |              | | |
/// +-----------------------------+   | device pixel scale |  | pattern transform |              | | |
///                                   | content origin     |  | color             |              | | |
///                                   +--------------------+  +-------------------+--------------+-+-+
///
/// To use the quad infrastructure, a shader must define the following entry
/// points in the corresponding shader stages:
/// - void pattern_vertex(PrimitiveInfo prim)
/// - vec4 pattern_fragment(vec4 base_color)
///```

// Default to a sampler2D source if the shader was compiled without an
// explicit texture-kind feature.
#if !defined(WR_FEATURE_TEXTURE_2D) && !defined(WR_FEATURE_TEXTURE_RECT) && !defined(WR_FEATURE_TEXTURE_EXTERNAL) && !defined(WR_FEATURE_TEXTURE_EXTERNAL_BT709) && !defined(WR_FEATURE_TEXTURE_EXTERNAL_ESSL1)
#define WR_FEATURE_TEXTURE_2D
#endif

#include shared,rect,transform,render_task,gpu_buffer

flat varying mediump vec4 v_color;
// z: is_mask
// w: has edge flags
// x,y are avaible for patterns to use.
flat varying lowp ivec4 v_flags;
#define v_flags_is_mask v_flags.z
#define v_flags_has_edge_mask v_flags.w


#ifndef SWGL_ANTIALIAS
varying highp vec2 vLocalPos;
#endif

#ifdef WR_VERTEX_SHADER

#define EDGE_AA_LEFT    1
#define EDGE_AA_TOP     2
#define EDGE_AA_RIGHT   4
#define EDGE_AA_BOTTOM  8

#define PART_CENTER     0
#define PART_LEFT       1
#define PART_TOP        2
#define PART_RIGHT      3
#define PART_BOTTOM     4
#define PART_ALL        5

#define QF_IS_OPAQUE            1
#define QF_APPLY_DEVICE_CLIP    2
#define QF_USE_AA_SEGMENTS      8
#define QF_IS_MASK              16

#define INVALID_SEGMENT_INDEX   0xff

#define AA_PIXEL_RADIUS 2.0

PER_INSTANCE in ivec4 aData;

struct QuadSegment {
    RectWithEndpoint rect;
    RectWithEndpoint uv_rect;
};

struct PrimitiveInfo {
    vec2 local_pos;

    RectWithEndpoint pattern_rect;
    RectWithEndpoint bounds;

    QuadSegment segment;

    int edge_flags;
    int quad_flags;
    ivec2 pattern_input;
};

struct QuadPrimitive {
    // The (clipped) coverage rect.
    RectWithEndpoint bounds;
    // The rect that situates the source pattern.
    RectWithEndpoint pattern_rect;
    RectWithEndpoint uv_rect;
    vec4 pattern_scale_offset;
    vec4 color;
};

QuadSegment fetch_segment(int base, int index) {
    QuadSegment seg;

    vec4 texels[2] = fetch_from_gpu_buffer_2f(base + 5 + index * 2);

    seg.rect = RectWithEndpoint(texels[0].xy, texels[0].zw);
    seg.uv_rect = RectWithEndpoint(texels[1].xy, texels[1].zw);

    return seg;
}

QuadPrimitive fetch_primitive(int index) {
    QuadPrimitive prim;

    vec4 texels[5] = fetch_from_gpu_buffer_5f(index);

    prim.bounds = RectWithEndpoint(texels[0].xy, texels[0].zw);
    prim.pattern_rect = RectWithEndpoint(texels[1].xy, texels[1].zw);
    prim.uv_rect = RectWithEndpoint(texels[2].xy, texels[2].zw);
    prim.pattern_scale_offset = texels[3];
    prim.color = texels[4];

    return prim;
}

struct QuadHeader {
    int transform_id;
    int z_id;
    ivec2 pattern_input;
};

QuadHeader fetch_header(int address) {
    ivec4 header = fetch_from_gpu_buffer_1i(address);

    QuadHeader qh = QuadHeader(
        header.x,
        header.y,
        header.zw
    );

    return qh;
}

struct QuadInstance {
    // x
    int prim_address_i;

    // y
    int prim_address_f;

    // z
    int quad_flags;
    int edge_flags;
    int part_index;
    int segment_index;

    // w
    int picture_task_address;
};

QuadInstance decode_instance() {
    QuadInstance qi = QuadInstance(
        aData.x,

        aData.y,

        (aData.z >> 24) & 0xff,
        (aData.z >> 16) & 0xff,
        (aData.z >>  8) & 0xff,
        (aData.z >>  0) & 0xff,

        aData.w
    );

    return qi;
}

struct VertexInfo {
    vec2 local_pos;
};

VertexInfo write_vertex(vec2 local_pos,
                        float z,
                        Transform transform,
                        vec2 content_origin,
                        RectWithEndpoint task_rect,
                        int quad_flags) {
    VertexInfo vi;

    // Transform the current vertex to device space.
    vec4 transformed = transform.m * vec4(local_pos, 0.0, 1.0);
    vec2 device_pos = transformed.xy;
    float w = transformed.w;

    if ((quad_flags & QF_APPLY_DEVICE_CLIP) != 0) {
        RectWithEndpoint device_clip_rect = RectWithEndpoint(
            content_origin,
            content_origin + task_rect.p1 - task_rect.p0
        );

        // Clip to task rect
        device_pos = rect_clamp(device_clip_rect, device_pos);

        vi.local_pos = (transform.inv_m * vec4(device_pos, 0.0, 1.0)).xy;
    } else {
        vi.local_pos = local_pos;
    }

    // Apply offsets for the render task to get correct screen location.
    vec2 final_offset = -content_origin + task_rect.p0;

    gl_Position = uTransform * vec4(device_pos + final_offset * w, z * w, w);

    return vi;
}

void pattern_vertex(PrimitiveInfo prim);

vec2 scale_offset_map_point(vec4 scale_offset, vec2 p) {
    return p * scale_offset.xy + scale_offset.zw;
}

RectWithEndpoint scale_offset_map_rect(vec4 scale_offset, RectWithEndpoint r) {
    vec2 p0 = scale_offset_map_point(scale_offset, r.p0);
    vec2 p1 = scale_offset_map_point(scale_offset, r.p1);
    return RectWithEndpoint(
        min(p0, p1),
        max(p0, p1)
    );
}

PrimitiveInfo quad_primive_info(void) {
    QuadInstance qi = decode_instance();

    QuadHeader qh = fetch_header(qi.prim_address_i);
    Transform transform = fetch_transform(qh.transform_id);
    PictureTask task = fetch_picture_task(qi.picture_task_address);
    QuadPrimitive prim = fetch_primitive(qi.prim_address_f);
    float z = float(qh.z_id);

    QuadSegment seg;
    if (qi.segment_index == INVALID_SEGMENT_INDEX) {
        seg.rect = prim.bounds;
        seg.uv_rect = prim.uv_rect;
    } else {
        seg = fetch_segment(qi.prim_address_f, qi.segment_index);
    }

    // The local space rect that we will draw, which is effectively:
    //  - The tile within the primitive we will draw
    //  - Intersected with any local-space clip rect(s)
    //  - Expanded for AA edges where appropriate
    RectWithEndpoint local_coverage_rect = seg.rect;

    // Clamp the (possibly per-segment) coverage rect to the primitive bounds.
    // Note: if we ensure on the CPU side that segments are contained in the
    // prim bounds we can get rif of this here.
    local_coverage_rect.p0 = max(local_coverage_rect.p0, prim.bounds.p0);
    local_coverage_rect.p1 = min(local_coverage_rect.p1, prim.bounds.p1);
    local_coverage_rect.p1 = max(local_coverage_rect.p0, local_coverage_rect.p1);

    float aa_left   = (qi.edge_flags & EDGE_AA_LEFT)   != 0 ? 1.0 : 0.0;
    float aa_top    = (qi.edge_flags & EDGE_AA_TOP)    != 0 ? 1.0 : 0.0;
    float aa_right  = (qi.edge_flags & EDGE_AA_RIGHT)  != 0 ? 1.0 : 0.0;
    float aa_bottom = (qi.edge_flags & EDGE_AA_BOTTOM) != 0 ? 1.0 : 0.0;

    switch (qi.part_index) {
        case PART_LEFT:
            local_coverage_rect.p1.x = local_coverage_rect.p0.x + AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            // The strip covers the full height of the primitive, so its top
            // and bottom rows lie on the primitive's top and bottom edges.
            swgl_antiAlias(EDGE_AA_LEFT | (qi.edge_flags & (EDGE_AA_TOP | EDGE_AA_BOTTOM)));
#else
            local_coverage_rect.p0.x -= AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y -= aa_top * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y += aa_bottom * AA_PIXEL_RADIUS;
#endif
            break;
        case PART_TOP:
            local_coverage_rect.p0.x += aa_left * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.x -= aa_right * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y = local_coverage_rect.p0.y + AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(EDGE_AA_TOP);
#else
            local_coverage_rect.p0.y -= AA_PIXEL_RADIUS;
#endif
            break;
        case PART_RIGHT:
            local_coverage_rect.p0.x = local_coverage_rect.p1.x - AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            // The strip covers the full height of the primitive, so its top
            // and bottom rows lie on the primitive's top and bottom edges.
            swgl_antiAlias(EDGE_AA_RIGHT | (qi.edge_flags & (EDGE_AA_TOP | EDGE_AA_BOTTOM)));
#else
            local_coverage_rect.p1.x += AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y -= aa_top * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y += aa_bottom * AA_PIXEL_RADIUS;
#endif
            break;
        case PART_BOTTOM:
            local_coverage_rect.p0.x += aa_left * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.x -= aa_right * AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y = local_coverage_rect.p1.y - AA_PIXEL_RADIUS;
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(EDGE_AA_BOTTOM);
#else
            local_coverage_rect.p1.y += AA_PIXEL_RADIUS;
#endif
            break;
        case PART_CENTER:
            local_coverage_rect.p0.x += aa_left * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.x -= aa_right * AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y += aa_top * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y -= aa_bottom * AA_PIXEL_RADIUS;
            break;
        case PART_ALL:
        default:
#ifdef SWGL_ANTIALIAS
            swgl_antiAlias(qi.edge_flags);
#else
            local_coverage_rect.p0.x -= aa_left * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.x += aa_right * AA_PIXEL_RADIUS;
            local_coverage_rect.p0.y -= aa_top * AA_PIXEL_RADIUS;
            local_coverage_rect.p1.y += aa_bottom * AA_PIXEL_RADIUS;
#endif
            break;
    }

    vec2 local_pos = mix(local_coverage_rect.p0, local_coverage_rect.p1, aPosition);

    VertexInfo vi = write_vertex(
        local_pos,
        z,
        transform,
        task.content_origin,
        task.task_rect,
        qi.quad_flags
    );

    v_color = prim.color;

    vec4 pattern_tx = prim.pattern_scale_offset;
    seg.rect = scale_offset_map_rect(pattern_tx, seg.rect);

    return PrimitiveInfo(
        scale_offset_map_point(pattern_tx, vi.local_pos),
        scale_offset_map_rect(pattern_tx, prim.pattern_rect),
        scale_offset_map_rect(pattern_tx, prim.bounds),
        seg,
        qi.edge_flags,
        qi.quad_flags,
        qh.pattern_input
    );
}

// Move the AA distance rect *away* from the primitive for edges that are
// not anti-aliased (by an arbitrary amount).
float edge_no_aa_offset(int edge, int flags) {
    return ((flags & edge) != 0) ? 0.0 : 10.0;
}

void antialiasing_vertex(PrimitiveInfo prim) {
#ifndef SWGL_ANTIALIAS
    // This does the setup that is required for init_tranform_vs.

    // The "transform bounds" define the edges along which anti-aliasing
    // is applied in the fragment shader.
    RectWithEndpoint xf_bounds = prim.bounds;

    // In order to prevent the edges with no anti-aliasing from getting
    // anti-aliased, we have to move the aa rect *away* from them.
    xf_bounds.p0.x -= edge_no_aa_offset(EDGE_AA_LEFT, prim.edge_flags);
    xf_bounds.p1.x += edge_no_aa_offset(EDGE_AA_RIGHT, prim.edge_flags);
    xf_bounds.p0.y -= edge_no_aa_offset(EDGE_AA_TOP, prim.edge_flags);
    xf_bounds.p1.y += edge_no_aa_offset(EDGE_AA_BOTTOM, prim.edge_flags);

    vTransformBounds = vec4(xf_bounds.p0, xf_bounds.p1);

    vLocalPos = prim.local_pos;

    if (prim.edge_flags == 0) {
        v_flags_has_edge_mask = 0;
    } else {
        v_flags_has_edge_mask = 1;
    }
#endif
}

void main() {
    PrimitiveInfo prim = quad_primive_info();

    if ((prim.quad_flags & QF_IS_MASK) != 0) {
        v_flags_is_mask = 1;
    } else {
        v_flags_is_mask = 0;
    }

    antialiasing_vertex(prim);
    pattern_vertex(prim);
}
#endif

#ifdef WR_FRAGMENT_SHADER
vec4 pattern_fragment(vec4 base_color);

float antialiasing_fragment() {
    float alpha = 1.0;
#ifndef SWGL_ANTIALIAS
    if (v_flags_has_edge_mask != 0) {
        alpha = rectangle_aa_fragment(vLocalPos);
    }
#endif
    return alpha;
}

void main() {
    vec4 base_color = v_color;
    base_color *= antialiasing_fragment();
    vec4 output_color = pattern_fragment(base_color);

    if (v_flags_is_mask != 0) {
        output_color = output_color.rrrr;
    }

    oFragColor = output_color;
}

#endif
