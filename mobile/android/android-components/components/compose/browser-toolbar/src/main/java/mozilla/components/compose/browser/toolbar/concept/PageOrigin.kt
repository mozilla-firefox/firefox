/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.concept

import androidx.annotation.StringRes
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.FadeDirection.FADE_DIRECTION_END
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.TextGravity.TEXT_GRAVITY_START
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent

/**
 * Details about the website origin.
 *
 * @property title The title of the website.
 * @property url The URL of the website.
 * @property hint Text displayed in the toolbar when there's no URL to display (e.g.: no tab or empty URL)
 * @property onClick [BrowserToolbarInteraction] describing how to handle any page origin detail being clicked.
 * @property onLongClick Optional [BrowserToolbarInteraction] describing how to handle any page origin detail
 * being long clicked.
 * @property fadeDirection The direction in which the text should fade.
 * @property textGravity The gravity of the text - whether to show the start or end of long text
 * that does not fit the available space.
 */
data class PageOrigin(
    @StringRes val hint: Int,
    val title: String?,
    val url: String?,
    val onClick: BrowserToolbarEvent,
    val onLongClick: BrowserToolbarInteraction? = null,
    val fadeDirection: FadeDirection = FADE_DIRECTION_END,
    val textGravity: TextGravity = TEXT_GRAVITY_START,
) {
    /**
     * Static values used in the configuration of the [PageOrigin] class.
     */
    companion object {
        /**
         * The direction in which the text should fade if it does not fit the available space.
         */
        enum class FadeDirection {
            /**
             * No text fading.
             */
            FADE_DIRECTION_NONE,

            /**
             * Text that overflows the available space should be faded at the start.
             */
            FADE_DIRECTION_START,

            /**
             * Text that overflows the available space should be faded at the end.
             */
            FADE_DIRECTION_END,
        }

        /**
         * The gravity of the text - whether to show the start or end of long text
         * that does not fit the available space.
         */
        enum class TextGravity {
            /**
             * Show the start of long text and hide the end part which overflows the available space.
             */
            TEXT_GRAVITY_START,

            /**
             * Show the end of long text and hide the start part which overflows the available space.
             */
            TEXT_GRAVITY_END,
        }
    }
}
