/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Rect
import java.io.InputStream
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements

/** The width the shadow reports for every image it decodes. */
internal const val SHADOW_WIDTH = 80

/** The height the shadow reports for every image it decodes. */
internal const val SHADOW_HEIGHT = 40

/**
 * Robolectric's default [BitmapFactory] shadow returns a bitmap from an `inJustDecodeBounds` decode. The real
 * [BitmapFactory] always returns null there and reports the size through the [BitmapFactory.Options] instead, so this
 * shadow restores that contract to keep decoding from treating the return value as a success signal.
 */
@Implements(BitmapFactory::class)
// Robolectric instantiates shadows, so this has to stay a class.
@Suppress("UtilityClassWithPublicConstructor")
class ShadowBoundsReportingBitmapFactory {
    companion object {
        @Implementation
        @JvmStatic
        // Some parameters are unused but method signature should be the same as for original class.
        @Suppress("UNUSED_PARAMETER")
        fun decodeStream(
            stream: InputStream?,
            outPadding: Rect?,
            opts: BitmapFactory.Options?,
        ): Bitmap? {
            stream?.readBytes()?.let {}

            if (opts?.inJustDecodeBounds == true) {
                opts.outWidth = SHADOW_WIDTH
                opts.outHeight = SHADOW_HEIGHT
                return null
            }

            val sampleSize = (opts?.inSampleSize ?: 1).coerceAtLeast(1)
            return Bitmap.createBitmap(SHADOW_WIDTH / sampleSize, SHADOW_HEIGHT / sampleSize, Bitmap.Config.ARGB_8888)
        }
    }
}
