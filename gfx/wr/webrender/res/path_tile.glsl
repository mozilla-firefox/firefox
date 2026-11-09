/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Rasterization of the path tiles produced by path_tiler.rs.

#include gpu_buffer

#define PATH_TILE_SIZE 16.0
// Edge coordinates have 15 steps per pixel (see EDGE_STEPS_PER_PIXEL in
// path_tiler.rs), and the unorm texels map 255 to 1.0.
#define PATH_EDGE_SCALE (255.0 / 15.0)
#define PATH_TILE_COORD_MASK 0x3FF

#define PATH_FILL_RULE_NONZERO 1
#define PATH_FILL_INVERTED 2

// The edges of all path tiles, one per texel. See GpuBufferBlockEdge.
uniform sampler2D sPathEdges;

#ifdef WR_VERTEX_SHADER

struct PathTile {
    // In pixels, relative to the origin of the render task.
    vec4 rect;
    // The range of the tile's edges in sPathEdges.
    ivec2 edges;
    int backdrop;
    int path_address;
};

// See PathTileInstance.
PathTile decode_path_tile(ivec4 data) {
    PathTile tile;

    vec2 offset = vec2(
        float((data.x >> 10) & PATH_TILE_COORD_MASK),
        float(data.x & PATH_TILE_COORD_MASK)
    );
    float extend_x = float((data.x >> 20) & PATH_TILE_COORD_MASK);
    tile.rect = vec4(
        offset.x,
        offset.y,
        offset.x + 1.0 + extend_x,
        offset.y + 1.0
    ) * PATH_TILE_SIZE;

    int edge_count = (data.z >> 16) & 0xFFFF;
    tile.edges = ivec2(data.y, data.y + edge_count);
    tile.backdrop = (data.z & 0xFFFF) - 128;
    tile.path_address = data.w;

    return tile;
}

struct PathInfo {
    int render_task_address;
    int fill_rule;
    float opacity;
};

// See PathInfo in path_tiler.rs.
PathInfo fetch_path_info(int address) {
    ivec4 data = fetch_from_gpu_buffer_1i(address);

    PathInfo path;
    path.render_task_address = data.x;
    path.fill_rule = data.y;
    path.opacity = float(data.z) / 65535.0;

    return path;
}

struct PathPrimInfo {
    int z_id;
    // Premultiplied.
    vec4 color;
    // In pixels, relative to the origin of the render task.
    vec4 scissor;
};

// See PathPrimInfo in path_tiler.rs. Stored in the two texels after the PathInfo.
PathPrimInfo fetch_path_prim_info(int address) {
    ivec4 data = fetch_from_gpu_buffer_1i(address + 1);
    ivec4 scissor = fetch_from_gpu_buffer_1i(address + 2);

    PathPrimInfo prim;
    prim.z_id = data.x;
    prim.color = vec4(
        float((data.y >> 16) & 0xFFFF),
        float(data.y & 0xFFFF),
        float((data.z >> 16) & 0xFFFF),
        float(data.z & 0xFFFF)
    ) / 65535.0;
    prim.scissor = vec4(scissor);

    return prim;
}

#endif

#ifdef WR_FRAGMENT_SHADER

#if !defined(WR_FEATURE_PATH_SSAA4) && !defined(WR_FEATURE_PATH_SSAA8)
float rasterize_edge_analytical(vec2 p0, vec2 p1) {
    // The overlap range on the y axis between the current row of pixels and the segment.
    // It can be a negative range (negative edge winding).
    float y0 = clamp(p0.y, 0.0, 1.0);
    float y1 = clamp(p1.y, 0.0, 1.0);

    if (y0 == y1) {
        return 0.0;
    }

    float inv_dy = 1.0 / (p1.y - p0.y);
    // The interpolation factors at the start and end of the intersection between the edge
    // and the row of pixels.
    float t0 = (y0 - p0.y) * inv_dy;
    float t1 = (y1 - p0.y) * inv_dy;
    // X positions at t0 and t1
    float x0 = p0.x * (1.0 - t0) + p1.x * t0;
    float x1 = p0.x * (1.0 - t1) + p1.x * t1;

    // Jitter to avoid NaN when dividing by xmin-xmax (for example vertical edges).
    float jitter = 1e-5;

    float xmin = min(min(x1, x0), 1.0) - jitter;
    float xmax = max(x1, x0);
    float b = min(xmax, 1.0);
    float c = max(b, 0.0);
    float d = max(xmin, 0.0);
    float area = (b + 0.5 * (d * d - c * c) - xmin) / (xmax - xmin);

    return area * (y1 - y0);
}
#endif

#if defined(WR_FEATURE_PATH_SSAA4) || defined(WR_FEATURE_PATH_SSAA8)
// The winding number contribution of one edge to one sample.
//
// `offset` is the sample's position, `edge` the vector from the upper to the lower
// endpoint of the edge, `winding` the edge's winding direction, and `cross_at_origin`
// the cross product of `edge` with the vector from the sample pattern's origin to the
// upper endpoint. `y_min` and `y_max` are the edge's y range in the sample pattern's
// coordinate system.
//
// The two quantities the test needs are affine in `offset`, so everything that does
// not depend on the sample is computed once per edge by the caller and only two fused
// multiply-adds and three comparisons are left here.
float ssaa_sample(
    vec2 offset,
    vec2 edge,
    float cross_at_origin,
    float y_min,
    float y_max,
    float winding
) {
    // The sample is between the two endpoints on the y axis.
    bool in_y_range = y_min <= offset.y && y_max >= offset.y;
    // The sample is on the right hand side of the edge.
    float cross = cross_at_origin + edge.x * offset.y - edge.y * offset.x;

    return (in_y_range && cross >= 0.0) ? winding : 0.0;
}
#endif

float path_even_odd(float winding_number) {
    return 1.0 - abs(mod(abs(winding_number), 2.0) - 1.0);
}

float path_non_zero(float winding_number) {
    return min(abs(winding_number), 1.0);
}

// The coverage of a pixel of a path tile.
//
// uv is the position of the pixel center relative to the tile's origin, in pixels.
float path_tile_coverage(
    vec2 uv,
    HIGHP_FS_ADDRESS ivec2 edges,
    int backdrop,
    int fill_rule
) {
#if defined(WR_FEATURE_PATH_SSAA4)
    vec4 wn = vec4(float(backdrop));
#elif defined(WR_FEATURE_PATH_SSAA8)
    vec4 wn0 = vec4(float(backdrop));
    vec4 wn1 = vec4(float(backdrop));
#else
    float winding_number = float(backdrop);
#endif

    // This isn't necessary but to be on the safe side and make sure we don't accidentally
    // hang from of some corrupted data, restrict the loop to a large-ish number of segments.
    HIGHP_FS_ADDRESS int end = min(edges.y, edges.x + 512);
    for (HIGHP_FS_ADDRESS int edge_idx = edges.x; edge_idx < end; edge_idx++) {
        ivec2 edge_uv = ivec2(
            int(uint(edge_idx) % WR_MAX_VERTEX_TEXTURE_WIDTH),
            int(uint(edge_idx) / WR_MAX_VERTEX_TEXTURE_WIDTH)
        );
        vec4 edge = texelFetch(sPathEdges, edge_uv, 0) * PATH_EDGE_SCALE;

#if defined(WR_FEATURE_PATH_SSAA4)
        vec2 p0 = edge.xy;
        vec2 p1 = edge.zw;

        // A horizontal edge contributes zero to the winding number of every
        // sample. Skipping it here also means the orientation below is decided
        // by a strict comparison.
        if (p0.y == p1.y) {
            continue;
        }

        bool downwards = p1.y > p0.y;
        float s = downwards ? 1.0 : -1.0;
        vec2 upper = (downwards ? p0 : p1) - uv;
        vec2 lower = (downwards ? p1 : p0) - uv;

        // The offsets below are applied to the edge rather than to the sample, so
        // the edge's y range in the sample pattern's coordinate system is the
        // negated and flipped y range of the edge.
        float y_min = -lower.y;
        float y_max = -upper.y;

        // The four samples sit on rows 3/8 and 1/8 above and below the pixel
        // center. An edge whose y range does not reach them covers none of them
        // and cannot change any of the winding numbers. For a given pixel most
        // edges of the tile are several rows away, so this is the common case.
        if (y_min <= 3.0/8.0 && y_max >= -3.0/8.0) {
            // Sample offsets:
            // +---+---+---+---+
            // |   |   | a |   |
            // +---+---+---+---+
            // | b |   |   |   |
            // +---+---o---+---+
            // |   |   |   | c |
            // +---+---+---+---+
            // |   | d |   |   |
            // +---+---+---+---+
            // These are offsets applied to the edge, so the sample pattern is this
            // grid mirrored through the pixel center. Both are rotated grids with one
            // sample per row and per column, which is what matters here.
            const vec2 a = vec2( 1.0/8.0, -3.0/8.0);
            const vec2 b = vec2(-3.0/8.0, -1.0/8.0);
            const vec2 c = vec2( 3.0/8.0,  1.0/8.0);
            const vec2 d = vec2(-1.0/8.0,  3.0/8.0);

            vec2 v = lower - upper;
            float cross_at_origin = v.x * upper.y - upper.x * v.y;

            wn.x += ssaa_sample(a, v, cross_at_origin, y_min, y_max, s);
            wn.y += ssaa_sample(b, v, cross_at_origin, y_min, y_max, s);
            wn.z += ssaa_sample(c, v, cross_at_origin, y_min, y_max, s);
            wn.w += ssaa_sample(d, v, cross_at_origin, y_min, y_max, s);
        }
#elif defined(WR_FEATURE_PATH_SSAA8)
        vec2 p0 = edge.xy;
        vec2 p1 = edge.zw;

        // See the comment in the ssaa4 branch.
        if (p0.y == p1.y) {
            continue;
        }

        bool downwards = p1.y > p0.y;
        float s = downwards ? 1.0 : -1.0;
        vec2 upper = (downwards ? p0 : p1) - uv;
        vec2 lower = (downwards ? p1 : p0) - uv;

        // See the comment in the ssaa4 branch.
        float y_min = -lower.y;
        float y_max = -upper.y;

        // The eight samples sit on rows 7/16, 5/16, 3/16 and 1/16 above and below
        // the pixel center. See the comment in the ssaa4 branch.
        if (y_min <= 7.0/16.0 && y_max >= -7.0/16.0) {
            // Sample offsets:
            // +---+---+---+---+---+---+---+---+
            // |   |   |   |   |   |   |   | a |
            // +---+---+---+---+---+---+---+---+
            // |   |   | b |   |   |   |   |   |
            // +---+---+---+---+---+---+---+---+
            // |   |   |   |   | c |   |   |   |
            // +---+---+---+---+---+---+---+---+
            // | d |   |   |   |   |   |   |   |
            // +---+---+---+---o---+---+---+---+
            // |   |   |   |   |   |   | e |   |
            // +---+---+---+---+---+---+---+---+
            // |   |   |   | f |   |   |   |   |
            // +---+---+---+---+---+---+---+---+
            // |   | g |   |   |   |   |   |   |
            // +---+---+---+---+---+---+---+---+
            // |   |   |   |   |   | h |   |   |
            // +---+---+---+---+---+---+---+---+
            // As in the ssaa4 branch, these are offsets applied to the edge, so the
            // sample pattern is this grid mirrored through the pixel center.
            const vec2 a = vec2( 7.0/16.0, -7.0/16.0);
            const vec2 b = vec2(-3.0/16.0, -5.0/16.0);
            const vec2 c = vec2( 1.0/16.0, -3.0/16.0);
            const vec2 d = vec2(-7.0/16.0, -1.0/16.0);
            const vec2 e = vec2( 5.0/16.0,  1.0/16.0);
            const vec2 f = vec2(-1.0/16.0,  3.0/16.0);
            const vec2 g = vec2(-5.0/16.0,  5.0/16.0);
            const vec2 h = vec2( 3.0/16.0,  7.0/16.0);

            vec2 v = lower - upper;
            float cross_at_origin = v.x * upper.y - upper.x * v.y;

            wn0.x += ssaa_sample(a, v, cross_at_origin, y_min, y_max, s);
            wn0.y += ssaa_sample(b, v, cross_at_origin, y_min, y_max, s);
            wn0.z += ssaa_sample(c, v, cross_at_origin, y_min, y_max, s);
            wn0.w += ssaa_sample(d, v, cross_at_origin, y_min, y_max, s);
            wn1.x += ssaa_sample(e, v, cross_at_origin, y_min, y_max, s);
            wn1.y += ssaa_sample(f, v, cross_at_origin, y_min, y_max, s);
            wn1.z += ssaa_sample(g, v, cross_at_origin, y_min, y_max, s);
            wn1.w += ssaa_sample(h, v, cross_at_origin, y_min, y_max, s);
        }
#else
        // rasterize_edge_analytical works in coordinates local to the pixel, with
        // the pixel covering the unit square, so shift by half a pixel to get the
        // top-left corner of the pixel.
        vec2 pixel_offset = uv - vec2(0.5);

        // Move to coordinates local to the current pixel.
        winding_number += rasterize_edge_analytical(
            edge.xy - pixel_offset,
            edge.zw - pixel_offset
        );
#endif
    }

    float mask;
#if defined(WR_FEATURE_PATH_SSAA4)
    if ((fill_rule & PATH_FILL_RULE_NONZERO) == 0) {
        mask = path_even_odd(wn.x) + path_even_odd(wn.y) + path_even_odd(wn.z) + path_even_odd(wn.w);
    } else {
        mask = path_non_zero(wn.x) + path_non_zero(wn.y) + path_non_zero(wn.z) + path_non_zero(wn.w);
    }

    mask *= 0.25;
#elif defined(WR_FEATURE_PATH_SSAA8)
    if ((fill_rule & PATH_FILL_RULE_NONZERO) == 0) {
        mask = path_even_odd(wn0.x) + path_even_odd(wn0.y) + path_even_odd(wn0.z) + path_even_odd(wn0.w)
            + path_even_odd(wn1.x) + path_even_odd(wn1.y) + path_even_odd(wn1.z) + path_even_odd(wn1.w);
    } else {
        mask = path_non_zero(wn0.x) + path_non_zero(wn0.y) + path_non_zero(wn0.z) + path_non_zero(wn0.w)
            + path_non_zero(wn1.x) + path_non_zero(wn1.y) + path_non_zero(wn1.z) + path_non_zero(wn1.w);
    }

    mask *= 0.125;
#else
    if ((fill_rule & PATH_FILL_RULE_NONZERO) == 0) {
        mask = path_even_odd(winding_number);
    } else {
        mask = path_non_zero(winding_number);
    }
#endif

    if ((fill_rule & PATH_FILL_INVERTED) != 0) {
        mask = 1.0 - mask;
    }

    return mask;
}

#endif
