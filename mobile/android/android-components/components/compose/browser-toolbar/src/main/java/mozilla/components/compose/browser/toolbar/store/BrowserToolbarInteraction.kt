/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.store

import android.graphics.drawable.Drawable
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.runtime.Immutable
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.lib.state.Action

/**
 * All possible ways to handle an interaction with browser toolbar elements.
 */
sealed interface BrowserToolbarInteraction {
    /**
     * [Action]s to be dispatched on [BrowserToolbarStore] when the user interacts with a toolbar element.
     */
    interface BrowserToolbarEvent : BrowserToolbarInteraction, BrowserToolbarAction {
        /**
         * Convenience method to combine dispatching a [BrowserToolbarEvent] with
         * showing a [BrowserToolbarMenu] for the same user interaction.
         *
         * @param menu [BrowserToolbarMenu] to show in addition to dispatching this event.
         */
        operator fun plus(menu: BrowserToolbarMenu) = CombinedEventAndMenu(
            event = this,
            menu = menu,
        )
    }

    /**
     * Popup menu to show when the user interacts with a toolbar element.
     */
    @Immutable
    fun interface BrowserToolbarMenu : BrowserToolbarInteraction {
        /**
         * List of items to show in the menu.
         */
        fun items(): List<BrowserToolbarMenuItem>
    }

    /**
     * Combined [BrowserToolbarEvent] to be dispatched and [BrowserToolbarMenu] to be shown
     * for the same user interaction.
     *
     * @param event [BrowserToolbarEvent] to be dispatched when the menu is shown.
     * @param menu [BrowserToolbarMenu] to show.
     */
    data class CombinedEventAndMenu(
        val event: BrowserToolbarEvent,
        val menu: BrowserToolbarMenu,
    ) : BrowserToolbarInteraction
}

/**
 * Items which can be shown in a [BrowserToolbarMenu].
 */
sealed class BrowserToolbarMenuItem {
    /**
     * Button to shown in a [BrowserToolbarMenu].
     *
     * @property icon Optional [Drawable] icon for the menu item.
     * @property iconResource Optional resource id of the icon to use for this button if a [Drawable] is not provided.
     * @property text Optional text for the menu item.
     * @property contentDescription Content description for this item. `null` if not important for accessibility.
     * @property onClick Optional [BrowserToolbarEvent] to be dispatched when this item is clicked.
     */
    data class BrowserToolbarMenuButton(
        val icon: Drawable? = null,
        @DrawableRes val iconResource: Int?,
        @StringRes val text: Int?,
        @StringRes val contentDescription: Int?,
        val onClick: BrowserToolbarEvent?,
    ) : BrowserToolbarMenuItem()

    /**
     * Divider to show in a [BrowserToolbarMenu].
     */
    data object BrowserToolbarMenuDivider : BrowserToolbarMenuItem()
}
