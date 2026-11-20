/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import kotlin.math.abs
import mozilla.components.feature.listentopage.R
import mozilla.components.ui.icons.R as iconsR

/**
 * How fast an article is read out, as a multiple of the speed the voice reads at.
 *
 * @property multiplier What the player is set to.
 */
enum class PlaybackSpeed(val multiplier: Float) {
    X0_25(0.25f),
    X0_5(0.5f),
    X0_75(0.75f),
    X1(1f),
    X1_25(1.25f),
    X1_5(1.5f),
    X1_75(1.75f),
    X2(2f);

    /** The step after this one, wrapping back to the slowest once past the fastest. */
    fun next(): PlaybackSpeed = entries[(ordinal + 1) % entries.size]

    /** What a screen reader says for this step. */
    @get:StringRes
    internal val contentDescription: Int
        get() =
            when (this) {
                X0_25 -> R.string.mozac_feature_listentopage_notification_playback_speed_0_25
                X0_5 -> R.string.mozac_feature_listentopage_notification_playback_speed_0_5
                X0_75 -> R.string.mozac_feature_listentopage_notification_playback_speed_0_75
                X1 -> R.string.mozac_feature_listentopage_notification_playback_speed_1
                X1_25 -> R.string.mozac_feature_listentopage_notification_playback_speed_1_25
                X1_5 -> R.string.mozac_feature_listentopage_notification_playback_speed_1_5
                X1_75 -> R.string.mozac_feature_listentopage_notification_playback_speed_1_75
                X2 -> R.string.mozac_feature_listentopage_notification_playback_speed_2
            }

    @get:DrawableRes
    internal val icon: Int
        get() =
            when (this) {
                X0_25 -> iconsR.drawable.mozac_ic_playback_speed_0_2x_24
                X0_5 -> iconsR.drawable.mozac_ic_playback_speed_0_5x_24
                X0_75 -> iconsR.drawable.mozac_ic_playback_speed_0_7x_24
                X1 -> iconsR.drawable.mozac_ic_playback_speed_1x_24
                X1_25 -> iconsR.drawable.mozac_ic_playback_speed_1_2x_24
                X1_5 -> iconsR.drawable.mozac_ic_playback_speed_1_5x_24
                X1_75 -> iconsR.drawable.mozac_ic_playback_speed_1_7x_24
                X2 -> iconsR.drawable.mozac_ic_playback_speed_2x_24
            }

    companion object {
        /** What an article opens at. */
        val Default = X1

        /**
         * The step nearest [multiplier].
         *
         * A speed that isn't in our list is reported as the one it is closest to, rather than as [Default], so that
         * [next] steps on from what the reader is hearing.
         */
        fun nearest(multiplier: Float): PlaybackSpeed = entries.minBy { abs(it.multiplier - multiplier) }
    }
}
