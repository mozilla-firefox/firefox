/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define WR_FEATURE_TEXTURE_2D

#include shared,prim_shared,image_source

// interpolated UV coordinates to sample.
varying highp vec2 vUv;

// Flag to allow perspective interpolation of UV.
// Packed in to a vector to work around bug 1630356.
flat varying mediump vec2 vPerspective;

flat varying highp vec4 vUvSampleBounds;

#ifdef WR_VERTEX_SHADER
struct SplitGeometry {
    vec2 local[4];
};

SplitGeometry fetch_split_geometry(int address) {
    vec4[2] data = fetch_from_gpu_buffer_2f(address);

    SplitGeometry geo;
    geo.local = vec2[4](
        data[0].xy,
        data[0].zw,
        data[1].xy,
        data[1].zw
    );

    return geo;
}

vec2 bilerp(vec2 a, vec2 b, vec2 c, vec2 d, float s, float t) {
    vec2 x = mix(a, b, t);
    vec2 y = mix(c, d, t);
    return mix(x, y, s);
}

struct SplitCompositeInstance {
    int prim_header_index;
    int polygons_address;
    float z;
    int render_task_index;
};

SplitCompositeInstance fetch_composite_instance() {
    SplitCompositeInstance ci;

    ci.prim_header_index = aData.x;
    ci.polygons_address = aData.y;
    ci.z = float(aData.z);
    ci.render_task_index = aData.w;

    return ci;
}

void main(void) {
    SplitCompositeInstance ci = fetch_composite_instance();
    SplitGeometry geometry = fetch_split_geometry(ci.polygons_address);
    PrimitiveHeader ph = fetch_prim_header(ci.prim_header_index);
    PictureTask dest_task = fetch_picture_task(ci.render_task_index);
    Transform transform = fetch_transform(ph.transform_id);
    ImageSource res = fetch_image_source(ph.user_data.x);
    ClipArea clip_area = fetch_clip_area(ph.user_data.w);

    vec2 dest_origin = dest_task.task_rect.p0 -
                       dest_task.content_origin;

    vec2 local_pos = bilerp(geometry.local[0], geometry.local[1],
                            geometry.local[3], geometry.local[2],
                            aPosition.y, aPosition.x);
    vec4 raster_pos = transform.m * vec4(local_pos, 0.0, 1.0);

    vec4 final_pos = vec4(
        dest_origin * raster_pos.w + raster_pos.xy * dest_task.device_pixel_scale,
        raster_pos.w * ci.z,
        raster_pos.w
    );

    write_clip(
        raster_pos,
        clip_area,
        dest_task
    );

    gl_Position = uTransform * final_pos;

    vec2 texture_size = vec2(TEX_SIZE(sColor0));
    vec2 uv0 = res.uv_rect.p0;
    vec2 uv1 = res.uv_rect.p1;

    vec2 min_uv = min(uv0, uv1);
    vec2 max_uv = max(uv0, uv1);

    vUvSampleBounds = vec4(
        min_uv + vec2(0.5),
        max_uv - vec2(0.5)
    ) / texture_size.xyxy;

    vec2 f = (local_pos - ph.pattern_rect.p0) / rect_size(ph.pattern_rect);
    f = get_image_quad_uv(ph.user_data.x, f);
    vec2 uv = mix(uv0, uv1, f);
    float perspective_interpolate = 1.0;

    vUv = uv / texture_size * mix(gl_Position.w, 1.0, perspective_interpolate);
    vPerspective.x = perspective_interpolate;
}
#endif

#ifdef WR_FRAGMENT_SHADER
void main(void) {
    float alpha = do_clip();
    float perspective_divisor = mix(gl_FragCoord.w, 1.0, vPerspective.x);
    vec2 uv = clamp(vUv * perspective_divisor, vUvSampleBounds.xy, vUvSampleBounds.zw);
    write_output(alpha * texture(sColor0, uv));
}

#ifdef SWGL_DRAW_SPAN
void swgl_drawSpanRGBA8() {
    float perspective_divisor = mix(swgl_forceScalar(gl_FragCoord.w), 1.0, vPerspective.x);
    vec2 uv = vUv * perspective_divisor;

    swgl_commitTextureRGBA8(sColor0, uv, vUvSampleBounds, SWGL_NO_CLIP_RECT);
}
#endif

#endif
