/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// This shader renders any kind of css gradents in a color or alpha target.

#include ps_quad
#include dithering

#define PI 3.141592653589793

#define GRADIENT_KIND_LINEAR 0
#define GRADIENT_KIND_RADIAL 1
#define GRADIENT_KIND_CONIC  2

// All of the integer varyings are packed into this header (see decode_gradient_header).
flat varying highp ivec4 v_gradient_header;
// Gradient-specific varying parameters are packed into these two varyings.
varying highp vec4 v_interpolated_data;
flat varying highp vec4 v_flat_data;
// The first four stop offsets provided to the fragment shader to reduce the
// number of gpu buffer reads in the common case.
flat varying mediump vec4 v_stop_offsets;
// Two color stops provided via varyings, only used in the fast path with only
// two color stops.
flat varying mediump vec4 v_color0;
flat varying mediump vec4 v_color1;

// v_flat_data.w is 1.0 if the stop colors are not premultiplied and must be
// premultiplied after interpolation, 0.0 otherwise.
#define v_alpha_mode v_flat_data.w

#ifdef WR_VERTEX_SHADER

void linear_gradient_vertex(vec2 position, vec4 data0) {
    vec2 p0 = data0.xy;
    vec2 p1 = data0.zw;

    vec2 dir = p1 - p0;
    dir = dir / dot(dir, dir);
    float offset = dot(p0, dir);

    v_interpolated_data = vec4(position, 0.0, 0.0);
    v_flat_data = vec4(dir, offset, 0.0);
}

void radial_gradient_vertex(vec2 position, vec4 data0, vec4 data1) {
    vec2 center = data0.xy;
    float start_radius = data1.x;
    float end_radius = data1.y;
    float xy_ratio = data1.z;

    // Store 1/rd where rd = end_radius - start_radius
    // If rd = 0, we can't get its reciprocal. Instead, just use a zero scale.
    float rd = end_radius - start_radius;
    float radius_scale = rd != 0.0 ? 1.0 / rd : 0.0;

    // Transform all coordinates by the y scale so the
    // fragment shader can work with circles

    // v_pos is in a coordinate space relative to the task rect
    // (so it is independent of the task origin).
    start_radius = start_radius * radius_scale;
    vec2 normalized_pos = (position - center) * radius_scale;
    normalized_pos.y *= xy_ratio;

    v_interpolated_data = vec4(normalized_pos.x, normalized_pos.y, 0.0, 0.0);
    v_flat_data = vec4(start_radius, 0.0, 0.0, 0.0);
}

void conic_gradient_vertex(vec2 position, vec4 data0, vec4 data1) {
    vec2 center = data0.xy;
    float start_offset = data1.x;
    float end_offset = data1.y;
    float angle = PI / 2.0 - data1.z;

    // Store 1/d where d = end_offset - start_offset
    // If d = 0, we can't get its reciprocal. Instead, just use a zero scale.
    float d = end_offset - start_offset;
    float offset_scale = d != 0.0 ? 1.0 / d : 0.0;

    start_offset = start_offset * offset_scale;
    vec2 dir = (position - center);

    v_interpolated_data = vec4(dir, start_offset, offset_scale);
    v_flat_data = vec4(angle, 0.0, 0.0, 0.0);
}

ivec4 decode_gradient_header(int base_address, vec4 payload) {
    int kind = int(payload.x);
    int count = int(payload.y);
    int extend_mode = int(payload.z);
    int colors_address = base_address + 1;

    return ivec4(
        kind,
        count,
        extend_mode,
        colors_address
    );
}

void pattern_vertex(PrimitiveInfo info) {
    int address = info.pattern_input.x;
    // gradient[0..1] contains linear/radial/conic specific data,
    // gradient[2] contains the header to interpret gradient stops.
    vec4[3] gradient = fetch_from_gpu_buffer_3f(address);
    ivec4 header = decode_gradient_header(address + 2, gradient[2]);

    switch (header.x) {
        case GRADIENT_KIND_LINEAR: {
            linear_gradient_vertex(info.local_pos, gradient[0]);
            break;
        }
        case GRADIENT_KIND_RADIAL: {
            radial_gradient_vertex(info.local_pos, gradient[0], gradient[1]);
            break;
        }
        case GRADIENT_KIND_CONIC: {
            conic_gradient_vertex(info.local_pos, gradient[0], gradient[1]);
            break;
        }
        default: {
            // This should be dead code.
            v_interpolated_data = vec4(0.0);
            v_flat_data = vec4(0.0);
            break;
        }
    }

    // Overwrites v_flat_data.w
    v_alpha_mode = gradient[2].w;

    int count = header.y;
    int colors_addr = header.w;
    int offsets_addrs = colors_addr + count;

    v_stop_offsets = fetch_from_gpu_buffer_1f(offsets_addrs);
    v_gradient_header = header;

    if (count == 2) {
        // Fast path: If we have only two color stops, pass them by varyings.
        vec4[2] colors = fetch_from_gpu_buffer_2f(colors_addr);
        v_color0 = colors[0];
        v_color1 = colors[1];
    }
}

#endif


#ifdef WR_FRAGMENT_SHADER

// From https://math.stackexchange.com/questions/1098487/atan2-faster-approximation
float approx_atan2(float y, float x) {
    vec2 a = abs(vec2(x, y));
    float slope = min(a.x, a.y) / max(a.x, a.y);
    float s2 = slope * slope;
    float r = ((-0.0464964749 * s2 + 0.15931422) * s2 - 0.327622764) * s2 * slope + slope;

    r = if_then_else(float(a.y > a.x), 1.57079637 - r, r);
    r = if_then_else(float(x < 0.0),   3.14159274 - r, r);
    // To match atan2's behavior, -0.0 should count as negative and flip the sign of r.
    // Does this matter in practice in the context of conic gradients?
    r = y < 0.0 ? -r : r;

    return r;
}

float apply_extend_mode(float offset) {
    // Handle the repeat mode.
    float mode = float(v_gradient_header.z);
    offset -= floor(offset) * mode;

    return offset;
}

// Sample the gradient using a sequence of gradient stops located at the provided
// addresses.
//
// See the comment above `prim_store::gradient::write_gpu_gradient_stops_tree` about
// the layout of the gradient data in the gpu buffer.
vec4 sample_gradient_stops_tree(float offset) {
    int count = v_gradient_header.y;
    HIGHP_FS_ADDRESS int colors_addr = v_gradient_header.w;
    // Address of the current level
    HIGHP_FS_ADDRESS int level_base_addr = colors_addr + count;
    // Number of blocks of 4 indices for the current level.
    // At the root, a single block is stored. Each level stores
    // 5 times more blocks than the previous one.
    int level_stride = 1;
    // Relative address within the current level.
    int offset_in_level = 0;
    // Current gradient stop index.
    int index = 0;
    // The index distance between consecutive stop offsets at
    // the current level. At the last level, the stride is 1.
    // each has a 5 times more stride than the next (so the
    // index stride starts high and is divided by 5 at each
    // iteration).
    int index_stride = 1;
    while (index_stride * 5 <= count) {
        index_stride *= 5;
    }

    // The offsets of the stops before and after the target offset.
    // They will converge to the correct values as the tree is
    // traversed.
    float prev_offset = 1.0;
    float next_offset = 0.0;

    // First offsets are the root level.
    vec4 current_stops = v_stop_offsets;

    // This could be a while(true) since we are going to exit this loop
    // its break statement, but we get miscompilations with while(true)
    // so instead we put a dummy condition that always evaluates to true.
    while (index_stride > 0) {
        // Determine which of the five partitions (sub-trees)
        // to take next.
        int next_partition = 4;
        if (current_stops.x > offset) {
            next_partition = 0;
            next_offset = current_stops.x;
        } else if (current_stops.y > offset) {
            next_partition = 1;
            prev_offset = current_stops.x;
            next_offset = current_stops.y;
        } else if (current_stops.z > offset) {
            next_partition = 2;
            prev_offset = current_stops.y;
            next_offset = current_stops.z;
        } else if (current_stops.w > offset) {
            next_partition = 3;
            prev_offset = current_stops.z;
            next_offset = current_stops.w;
        } else {
            prev_offset = current_stops.w;
        }

        index += next_partition * index_stride;

        if (index_stride == 1) {
            // If the index stride is 1, we visited a leaf,
            // we are done.
            break;
        }

        index_stride /= 5;
        level_base_addr += level_stride;
        level_stride *= 5;
        offset_in_level = offset_in_level * 5 + next_partition;

        // Fetch new offsets for the next iteration.
        current_stops = fetch_from_gpu_buffer_1f(level_base_addr + offset_in_level);
    }

    // If we are before the first gradient stop, next_offset and prev_offset
    // will be equal, in which case we want the contribution of the first stop, so
    // the interpolaiton factor remains zero.
    float d = next_offset - prev_offset;
    float factor = 0.0;
    if (index >= count) {
        // The current offset is after the last gradient stop.
        factor = 1.0;
    } else if (d > 0.0) {
        factor = clamp((offset - prev_offset) / d, 0.0, 1.0);
    }

    // TODO: This is clamp(index, 1, count - 1) but swgl
    // does not have the clamp overload for ints.
    if (index < 1) {
        index = 1;
    } else if (index > count - 1) {
        index = count - 1;
    }
    HIGHP_FS_ADDRESS int color_pair_address = colors_addr + index - 1;
    vec4 color_pair[2] = fetch_from_gpu_buffer_2f(color_pair_address);

    return mix(color_pair[0], color_pair[1], factor);
}

// A fast path for sampling no more than two gradient stops.
//
// This version reads data from varyings instead of the gpu_buffer.
vec4 sample_gradient_stops_fast(float offset) {
    float d = v_stop_offsets.y - v_stop_offsets.x;
    float factor = 0.0;

    if (offset < v_stop_offsets.x) {
        factor = 0.0;
        d = 1.0;
    } else if (offset > v_stop_offsets.y) {
        factor = 1.0;
        d = 1.0;
    } else if (d > 0.0) {
        factor = clamp((offset - v_stop_offsets.x) / d, 0.0, 1.0);
    }

    return mix(v_color0, v_color1, factor);
}

float linear_gradient_fragment() {
    vec2 pos = v_interpolated_data.xy;
    vec2 scale_dir = v_flat_data.xy;
    float start_offset = v_flat_data.z;

    // Project position onto a direction vector to compute offset.
    float offset = dot(pos, scale_dir) - start_offset;

    // Due to precision issues with interpolated varyings if a row/column or diagonal
    // of pixels are exactly on the hard stop boundary, pixels along the hard stop may
    // fall on either side of the boundary inconsistently which creates a very noticeable
    // jagged look. The issue is hardware-dependent but has been observed with multiple
    // GPU vendors.
    // We work around it by adding a tiny amount to the offset as it makes it much less
    // likely for typical gradient stop offset values to land exactly on pixel centers.
    return offset + 0.000001;
}

float radial_gradient_fragment() {
    // Solve for t in length(pos) = start_radius + t * rd
    vec2 pos = v_interpolated_data.xy;
    float start_radius = v_flat_data.x;

    return length(pos) - start_radius;
}

float conic_gradient_fragment() {
    vec2 current_dir = v_interpolated_data.xy;
    float start_offset = v_interpolated_data.z;
    float offset_scale = v_interpolated_data.w;
    float angle = v_flat_data.x;

    float current_angle = approx_atan2(current_dir.y, current_dir.x) + angle;
    return fract(current_angle / (2.0 * PI)) * offset_scale - start_offset;
}

vec4 pattern_fragment(vec4 color) {

    float offset = 0.0;
    switch (v_gradient_header.x) {
        case GRADIENT_KIND_LINEAR: {
            offset = linear_gradient_fragment();
            break;
        }
        case GRADIENT_KIND_RADIAL: {
            offset = radial_gradient_fragment();
            break;
        }
        case GRADIENT_KIND_CONIC: {
            offset = conic_gradient_fragment();
            break;
        }
        default: {
            break;
        }
    }

    offset = apply_extend_mode(offset);

    int stop_count = v_gradient_header.y;
    vec4 stop_color;
    if (stop_count <= 2) {
        stop_color = sample_gradient_stops_fast(offset);
    } else {
        stop_color = sample_gradient_stops_tree(offset);
    }

    stop_color.rgb *= mix(1.0, stop_color.a, v_alpha_mode);

    return dither(color * stop_color);
}

#if defined(SWGL_DRAW_SPAN)
void swgl_drawSpanRGBA8() {
    int kind = v_gradient_header.x;
    if (kind != GRADIENT_KIND_LINEAR && kind != GRADIENT_KIND_RADIAL) {
        return;
    }

    int stop_count = v_gradient_header.y;
    int colors_address = v_gradient_header.w;
    int colors_addr = swgl_validateGradientFromStops(sGpuBufferF, get_gpu_buffer_uv(colors_address),
                                                     stop_count);
    if (colors_addr < 0) {
        // The gradient is invalid, this should not happen. We can't fall back to
        // the regular shader code because it expects the gradient stop offsets
        // to be laid out for a tree traversal but we laid them out linearly because
        // that's the fastest way for the span shader.
        // Replace the gradient with a pink solid color.
        swgl_commitSolidRGBA8(vec4(1.0, 0.0, 1.0, 1.0));
        return;
    }

    int offsets_addr = colors_addr + stop_count * 4;
    vec2 pos = v_interpolated_data.xy;
    bool repeat = v_gradient_header.z != 0.0;
    bool premultiply = v_alpha_mode != 0.0;

    if (kind == GRADIENT_KIND_LINEAR) {
        vec2 scale_dir = v_flat_data.xy;
        float start_offset = v_flat_data.z;

#ifdef WR_FEATURE_DITHERING
        swgl_commitDitheredLinearGradientFromStopsRGBA8(sGpuBufferF, offsets_addr, colors_addr,
            stop_count, repeat, pos, scale_dir, start_offset, gl_FragCoord, premultiply);
#else
        swgl_commitLinearGradientFromStopsRGBA8(sGpuBufferF, offsets_addr, colors_addr,
            stop_count, repeat, pos, scale_dir, start_offset, premultiply);
#endif
    } else if (kind == GRADIENT_KIND_RADIAL) {
      float start_radius = v_flat_data.x;

#ifdef WR_FEATURE_DITHERING
      swgl_commitDitheredRadialGradientFromStopsRGBA8(sGpuBufferF, offsets_addr, colors_addr,
          stop_count, repeat, pos, start_radius, premultiply);
#else
      swgl_commitRadialGradientFromStopsRGBA8(sGpuBufferF, offsets_addr, colors_addr,
          stop_count, repeat, pos, start_radius, premultiply);
#endif
    }
}
#endif

#endif
