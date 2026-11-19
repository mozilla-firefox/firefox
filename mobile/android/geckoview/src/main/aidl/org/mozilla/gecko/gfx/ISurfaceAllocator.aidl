/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import org.mozilla.gecko.gfx.GeckoSurface;

interface ISurfaceAllocator {
    GeckoSurface acquireSurface(in int width, in int height, in boolean singleBufferMode);
    void releaseSurface(in long handle);
}
