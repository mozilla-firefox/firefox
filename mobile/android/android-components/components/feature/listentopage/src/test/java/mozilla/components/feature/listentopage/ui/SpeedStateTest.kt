/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import java.util.Locale
import mozilla.components.compose.base.text.Text
import mozilla.components.feature.listentopage.R
import mozilla.components.feature.listentopage.playback.PlaybackSpeed
import org.junit.Assert.assertEquals
import org.junit.Test

private const val LABEL_PATTERN = "%1\$s×"

class SpeedStateTest {

    @Test
    fun `test that the menu lists every speed with the default one marked`() {
        val items = PlaybackSpeed.X1_5.toSpeedState(Locale.US, LABEL_PATTERN) {}.menuItems

        assertEquals(
            listOf(
                Text.String("0.25×"),
                Text.String("0.5×"),
                Text.String("0.75×"),
                Text.Resource(R.string.mozac_feature_listentopage_playback_speed_option_default),
                Text.String("1.25×"),
                Text.String("1.5×"),
                Text.String("1.75×"),
                Text.String("2×"),
            ),
            items.map { it.text },
        )
        assertEquals(listOf(PlaybackSpeed.X1_5), PlaybackSpeed.entries.filterIndexed { i, _ -> items[i].isChecked })
    }

    @Test
    fun `test that the speeds use the decimal separator of the locale`() {
        val items = PlaybackSpeed.Default.toSpeedState(Locale.GERMANY, LABEL_PATTERN) {}.menuItems

        assertEquals(Text.String("0,25×"), items[PlaybackSpeed.X0_25.ordinal].text)
        assertEquals(Text.String("1,75×"), items[PlaybackSpeed.X1_75.ordinal].text)
    }
}
