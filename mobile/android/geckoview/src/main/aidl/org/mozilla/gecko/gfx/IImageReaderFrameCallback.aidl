/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.gecko.gfx;
oneway interface IImageReaderFrameCallback {
    void releaseFrame(in long handle, in long readerId, in long frameId, in boolean render);
}
