/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.utils.ColorUtils.calculateAlphaFromPercentage
import mozilla.components.support.utils.ColorUtils.darken
import mozilla.components.support.utils.ColorUtils.lighten
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import androidx.compose.ui.graphics.Color as ComposeColor

@RunWith(AndroidJUnit4::class)
class ColorUtilsTest {

    @Test
    fun getReadableTextColor() {
        assertEquals(Color.BLACK.toLong(), ColorUtils.getReadableTextColor(Color.WHITE).toLong())
        assertEquals(Color.WHITE.toLong(), ColorUtils.getReadableTextColor(Color.BLACK).toLong())

        // Slack
        assertEquals(Color.BLACK.toLong(), ColorUtils.getReadableTextColor(-0x90b14).toLong())

        // Google+
        assertEquals(Color.WHITE.toLong(), ColorUtils.getReadableTextColor(-0x24bbc9).toLong())

        // Telegram
        assertEquals(Color.WHITE.toLong(), ColorUtils.getReadableTextColor(-0xad825d).toLong())

        // IRCCloud
        assertEquals(Color.BLACK.toLong(), ColorUtils.getReadableTextColor(-0xd0804).toLong())

        // Yahnac
        assertEquals(Color.WHITE.toLong(), ColorUtils.getReadableTextColor(-0xa8400).toLong())
    }

    @Test
    fun isDark() {
        assertTrue(ColorUtils.isDark(Color.BLACK))
        assertTrue(ColorUtils.isDark(Color.GRAY))
        assertTrue(ColorUtils.isDark(Color.DKGRAY))
        assertTrue(ColorUtils.isDark(Color.RED))
        assertTrue(ColorUtils.isDark(Color.BLUE))
        assertTrue(ColorUtils.isDark(Color.MAGENTA))

        assertFalse(ColorUtils.isDark(Color.GREEN))
        assertFalse(ColorUtils.isDark(Color.YELLOW))
        assertFalse(ColorUtils.isDark(Color.LTGRAY))
        assertFalse(ColorUtils.isDark(Color.CYAN))
        assertFalse(ColorUtils.isDark(Color.WHITE))
        assertFalse(ColorUtils.isDark(Color.TRANSPARENT))
    }

    @Test
    fun `GIVEN a number in the range of 0 to 100 WHEN calculateAlphaFromPercentage is called THEN alpha value in the range of 0 to 255 is returned`() {
        for (i in 0..100) {
            val result = calculateAlphaFromPercentage(i)
            val expectedResult = i * 255 / 100
            assertEquals(expectedResult, result)
        }
    }

    @Test
    fun `GIVEN opacity is less than zero WHEN calculateAlphaFromPercentage is called THEN alpha value in zero`() {
        for (i in -1..-100) {
            val result = calculateAlphaFromPercentage(i)
            assertEquals(0, result)
        }
    }

    @Test
    fun `GIVEN opacity is more than 100 WHEN calculateAlphaFromPercentage is called THEN alpha value in at max value`() {
        for (i in 256..355) {
            val result = calculateAlphaFromPercentage(i)
            assertEquals(255, result)
        }
    }

    @Test
    fun `GIVEN a red color and a factor of 0 WHEN lighten is called THEN the color is red`() {
        assertEquals(ComposeColor.Red, ComposeColor.Red.lighten(0f))
    }

    @Test
    @OptIn(ExperimentalStdlibApi::class)
    fun `GIVEN a red color and a factor of 0,5 WHEN lighten is called THEN the result is pink`() {
        assertEquals("ffffa191", ComposeColor.Red.lighten(0.5f).toArgb().toHexString(HexFormat.Default))
    }

    @Test
    fun `GIVEN a red color and a factor of 1 WHEN lighten is called THEN the result is white`() {
        assertEquals(ComposeColor.White, ComposeColor.Red.lighten(1f))
    }

    @Test
    fun `GIVEN a yellow color and a factor of 0 WHEN darken is called THEN the color is yellow`() {
        assertEquals(ComposeColor.Yellow, ComposeColor.Yellow.darken(0f))
    }

    @Test
    @OptIn(ExperimentalStdlibApi::class)
    fun `GIVEN a red color and a factor of 0,5 WHEN darken is called THEN the result is olive`() {
        assertEquals("ff636300", ComposeColor.Yellow.darken(0.5f).toArgb().toHexString(HexFormat.Default))
    }

    @Test
    fun `GIVEN a red color and a factor of 1 WHEN darken is called THEN the result is black`() {
        assertEquals(ComposeColor.Black, ComposeColor.Yellow.darken(1f))
    }
}
