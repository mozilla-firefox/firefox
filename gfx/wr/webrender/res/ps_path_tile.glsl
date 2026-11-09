/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Fills a path tile with a solid color, directly in a picture task.

#include shared,rect,render_task,path_tile

// Position in pixels relative to the tile's origin.
varying highp vec2 vUv;
// The tile's edge range, backdrop and fill rule.
flat varying highp ivec4 vTileData;
// Premultiplied.
flat varying mediump vec4 vColor;

#ifdef WR_VERTEX_SHADER

PER_INSTANCE in ivec4 aData;

void main(void) {
    PathTile tile = decode_path_tile(aData);
    PathInfo path = fetch_path_info(tile.path_address);
    PathPrimInfo prim = fetch_path_prim_info(tile.path_address);
    RectWithEndpoint task_rect = fetch_render_task_rect(path.render_task_address);

    // Tiles along the edges of the scissor rect can extend past it.
    vec2 p0 = clamp(tile.rect.xy, prim.scissor.xy, prim.scissor.zw);
    vec2 p1 = clamp(tile.rect.zw, prim.scissor.xy, prim.scissor.zw);
    vec2 pos = mix(p0, p1, aPosition.xy);

    vUv = pos - tile.rect.xy;
    vTileData = ivec4(tile.edges, tile.backdrop, path.fill_rule);
    vColor = prim.color;

    gl_Position = uTransform * vec4(task_rect.p0 + pos, float(prim.z_id), 1.0);
}

#endif

#ifdef WR_FRAGMENT_SHADER

void main(void) {
    float coverage = path_tile_coverage(vUv, vTileData.xy, vTileData.z, vTileData.w);
    oFragColor = vColor * coverage;
}

#endif
