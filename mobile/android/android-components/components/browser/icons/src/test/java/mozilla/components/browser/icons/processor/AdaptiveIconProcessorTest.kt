/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.icons.processor

import androidx.core.graphics.createBitmap
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.browser.icons.Icon
import mozilla.components.browser.icons.IconRequest
import mozilla.components.browser.icons.IconRequest.Resource.Type.MANIFEST_ICON
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.robolectric.annotation.Config

@RunWith(AndroidJUnit4::class)
class AdaptiveIconProcessorTest {

    @Test
    @Config(sdk = [26])
    fun `process adds padding to legacy icons`() {
        val bitmap = spy(createBitmap(128, 128))

        val icon = AdaptiveIconProcessor().process(
            mock(),
            mock(),
            IconRequest.Resource("", MANIFEST_ICON, maskable = false),
            Icon(bitmap, source = Icon.Source.DISK),
            mock(),
        )

        assertEquals(228, icon.bitmap.width)
        assertEquals(228, icon.bitmap.height)

        assertEquals(Icon.Source.DISK, icon.source)
        assertTrue(icon.maskable)
        verify(bitmap).recycle()
    }

    @Test
    fun `process adjusts the size of maskable icons`() {
        val bitmap = createBitmap(256, 256)

        val icon = AdaptiveIconProcessor().process(
            mock(),
            mock(),
            IconRequest.Resource("", MANIFEST_ICON, maskable = true),
            Icon(bitmap, source = Icon.Source.INLINE),
            mock(),
        )

        assertEquals(334, icon.bitmap.width)
        assertEquals(334, icon.bitmap.height)

        assertEquals(Icon.Source.INLINE, icon.source)
        assertTrue(icon.maskable)
    }

    @Test
    fun `process returns initial icon if bitmap is recycled`() {
        val bitmap = createBitmap(128, 128).apply { recycle() }
        val icon = Icon(bitmap, source = Icon.Source.INLINE)

        val processed = AdaptiveIconProcessor().process(
            mock(),
            mock(),
            IconRequest.Resource("", MANIFEST_ICON, maskable = true),
            icon,
            mock(),
        )

        assertEquals(icon, processed)
    }
}
