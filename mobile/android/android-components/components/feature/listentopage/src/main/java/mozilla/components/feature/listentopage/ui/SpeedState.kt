/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import java.text.NumberFormat
import java.util.Locale
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.feature.listentopage.R
import mozilla.components.feature.listentopage.playback.PlaybackSpeed

/**
 * What the playback speed control shows.
 *
 * @property icon The icon of the selected speed.
 * @property contentDescription What a screen reader says for the selected speed.
 * @property menuItems The speeds to pick from, with the selected one checked.
 */
internal data class SpeedState(
    @param:DrawableRes val icon: Int,
    @param:StringRes val contentDescription: Int,
    val menuItems: List<MenuItem.CheckableItem>,
)

/**
 * Builds the [SpeedState] of the speed control when this is the selected speed.
 *
 * @param locale The locale to format the speeds in the menu for, so that "0.25" reads "0,25" where a comma is the
 *   decimal separator.
 * @param labelPattern The unformatted label of a speed in the menu, such as "%1$s×", which the speed is formatted into.
 * @param onSpeedClick Invoked when the user picks a speed from the menu.
 */
internal fun PlaybackSpeed.toSpeedState(
    locale: Locale,
    labelPattern: String,
    onSpeedClick: (PlaybackSpeed) -> Unit,
): SpeedState {
    val numberFormat = NumberFormat.getNumberInstance(locale).apply { maximumFractionDigits = 2 }
    return SpeedState(
        icon = icon,
        contentDescription = contentDescription,
        menuItems =
            PlaybackSpeed.entries.map { speed ->
                MenuItem.CheckableItem(
                    text =
                        if (speed == PlaybackSpeed.Default) {
                            Text.Resource(R.string.mozac_feature_listentopage_playback_speed_option_default)
                        } else {
                            Text.String(String.format(locale, labelPattern, numberFormat.format(speed.multiplier)))
                        },
                    isChecked = speed == this,
                    onClick = { onSpeedClick(speed) },
                )
            },
    )
}
