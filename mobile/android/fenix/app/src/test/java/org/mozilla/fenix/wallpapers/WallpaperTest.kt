/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.wallpapers

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WallpaperTest {
    @Test
    fun `GIVEN the solid wallpaper is set to be shown WHEN checking whether the current wallpaper is solid THEN return true`() {
        val result = Wallpaper.nameIsSolid(Wallpaper.SOLID)

        assertTrue(result)
    }

    @Test
    fun `GIVEN a custom wallpaper is set to be shown WHEN checking whether the current wallpaper is solid THEN return false`() {
        val result = Wallpaper.nameIsSolid("wally world")

        assertFalse(result)
    }

    @Test
    fun `GIVEN the legacy wallpaper name is default WHEN migrating it THEN return the solid wallpaper name`() {
        assertEquals(Wallpaper.SOLID, Wallpaper.migrateLegacyName("default"))
    }

    @Test
    fun `GIVEN the legacy wallpaper name is NONE WHEN migrating it THEN return the solid wallpaper name`() {
        assertEquals(Wallpaper.SOLID, Wallpaper.migrateLegacyName("NONE"))
    }

    @Test
    fun `GIVEN an empty wallpaper name WHEN migrating it THEN return the solid wallpaper name`() {
        assertEquals(Wallpaper.SOLID, Wallpaper.migrateLegacyName(""))
    }

    @Test
    fun `GIVEN the legacy wallpaper name is edge-to-edge WHEN migrating it THEN return the gradient wallpaper name`() {
        assertEquals(Wallpaper.GRADIENT, Wallpaper.migrateLegacyName("edge-to-edge"))
    }

    @Test
    fun `GIVEN any other wallpaper name WHEN migrating it THEN return it unchanged`() {
        assertEquals("amethyst", Wallpaper.migrateLegacyName("amethyst"))
        assertEquals(Wallpaper.SOLID, Wallpaper.migrateLegacyName(Wallpaper.SOLID))
    }
}
