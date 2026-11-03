/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Draws the coverage of a path tile into a render task.
///
/// Used to apply path clips by filling the outside of the path: alpha holds
/// its coverage for the premultiplied dest-out blend mode, and the color
/// holds one minus it for the multiply blend mode.

#include shared,rect,render_task,path_tile

// Position in pixels relative to the tile's origin.
varying highp vec2 vUv;
// The tile's edge range, backdrop and fill rule.
flat varying highp ivec4 vTileData;

#ifdef WR_VERTEX_SHADER

PER_INSTANCE in ivec4 aData;

void main(void) {
    PathTile tile = decode_path_tile(aData);
    PathInfo path = fetch_path_info(tile.path_address);
    RectWithEndpoint task_rect = fetch_render_task_rect(path.render_task_address);

    // The last row and column of tiles can extend past the render task.
    vec2 task_size = task_rect.p1 - task_rect.p0;
    vec2 p0 = min(tile.rect.xy, task_size);
    vec2 p1 = min(tile.rect.zw, task_size);
    vec2 pos = mix(p0, p1, aPosition.xy);

    vUv = pos - tile.rect.xy;
    vTileData = ivec4(tile.edges, tile.backdrop, path.fill_rule);

    gl_Position = uTransform * vec4(task_rect.p0 + pos, 0.0, 1.0);
}

#endif

#ifdef WR_FRAGMENT_SHADER

void main(void) {
    float mask = path_tile_coverage(vUv, vTileData.xy, vTileData.z, vTileData.w);
    oFragColor = vec4(vec3(1.0 - mask), mask);
}

#ifdef SWGL_DRAW_SPAN
#if defined(WR_FEATURE_PATH_SSAA8)
#define PATH_SAMPLES 8
#elif defined(WR_FEATURE_PATH_SSAA4)
#define PATH_SAMPLES 4
#endif

#ifdef PATH_SAMPLES
void swgl_drawSpanRGBA8() {
    swgl_commitPathTileRGBA8(sPathEdges, vUv, vTileData, PATH_SAMPLES);
}

void swgl_drawSpanR8() {
    swgl_commitPathTileR8(sPathEdges, vUv, vTileData, PATH_SAMPLES);
}
#endif
#endif

#endif
