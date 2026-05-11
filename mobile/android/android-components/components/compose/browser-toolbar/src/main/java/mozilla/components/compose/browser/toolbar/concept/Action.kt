/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.concept

import android.graphics.drawable.Drawable
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu

/**
 * Actions that can be added to the toolbar.
 */
sealed class Action {
    /**
     * An action button to be added to the toolbar that can be configures with resource ids.
     *
     * @property drawableResId The icon resource to be displayed for the action button.
     * @property contentDescription The content description for the action button.
     * @property state the current [State] of the action button.
     * @property highlighted Whether or not to highlight this button.
     * @property onClick [BrowserToolbarInteraction] describing how to handle this button being clicked.
     * @property onLongClick Optional [BrowserToolbarInteraction] describing how to handle this button
     * being long clicked.
     */
    data class ActionButtonRes(
        @param:DrawableRes val drawableResId: Int,
        @param:StringRes val contentDescription: Int,
        val state: ActionButton.State = ActionButton.State.DEFAULT,
        val highlighted: Boolean = false,
        val onClick: BrowserToolbarInteraction,
        val onLongClick: BrowserToolbarInteraction? = null,
    ) : Action()

    /**
     * An action button to be added to the toolbar.
     *
     * @property drawable A [Drawable] to use as icon for this button.
     * @property shouldTint Whether or not to apply the application default tint to this icon.
     * @property contentDescription A [String] to use as content description for this button.
     * @property state the current [State] of the action button.
     * @property highlighted Whether or not to highlight this button.
     * @property onClick [BrowserToolbarInteraction] describing how to handle this button being clicked.
     * @property onLongClick Optional [BrowserToolbarInteraction] describing how to handle this button
     * being long clicked.
     */
    data class ActionButton(
        val drawable: Drawable?,
        val shouldTint: Boolean = true,
        val contentDescription: String,
        val state: State = State.DEFAULT,
        val highlighted: Boolean = false,
        val onClick: BrowserToolbarInteraction,
        val onLongClick: BrowserToolbarInteraction? = null,
    ) : Action() {

        /**
         * An enum class defining the current state of the [ActionButton].
         */
        enum class State {
            DEFAULT, DISABLED, ACTIVE,
        }
    }

    /**
     * An action button styled as a search selector to be added to the toolbar, that upon pressed will
     * automatically show a dropdown menu built from the passed in [menu] configuration.
     * This wraps the provided [icon] at the start with a down arrow to it's end
     * to indicate that clicking this will open a dropdown menu.
     *
     * @property icon A [Drawable] or [DrawableRes] to use as icon for this button.
     * @property contentDescription A [String] or [StringRes] to use as content description for this button.
     * @property menu The [BrowserToolbarMenu] to show when this button is clicked.
     * @property onClick Optional [BrowserToolbarEvent] to be dispatched when this button is clicked.
     */
    data class SearchSelectorAction(
        val icon: Icon,
        val contentDescription: ContentDescription,
        val menu: BrowserToolbarMenu,
        val onClick: BrowserToolbarEvent?,
    ) : Action() {

        /**
         * The image to use as icon for this button.
         */
        sealed interface Icon {
            /**
             *  The [Drawable] as icon for this button.
             *
             *  @property drawable The [Drawable] to use as icon.
             *  @property shouldTint Whether or not to apply the application default tint to this icon.
             */
            data class DrawableIcon(
                val drawable: Drawable,
                val shouldTint: Boolean = true,
            ) : Icon

            /**
             * The [DrawableRes] as icon for this button.
             */
            @JvmInline
            value class DrawableResIcon(
                @param:DrawableRes val resourceId: Int,
            ) : Icon
        }

        /**
         * The text that this button should display.
         */
        sealed interface Text {
            /**
             * The [String] to display in this this button.
             */
            @JvmInline
            value class StringText(val text: String) : Text

            /**
             * The [StringRes] to display as text in this button.
             */
            @JvmInline
            value class StringResText(
                @param:StringRes val resourceId: Int,
            ) : Text
        }

        /**
         * The content description menu item.
         */
        sealed interface ContentDescription {
            /**
             * The [String] to use as content description of this button.
             */
            @JvmInline
            value class StringContentDescription(val text: String) : ContentDescription

            /**
             * The [StringRes] to use as content description of this button.
             */
            @JvmInline
            value class StringResContentDescription(
                @param:StringRes val resourceId: Int,
            ) : ContentDescription
        }
    }

    /**
     * An action button styled as a tab counter to be added to the toolbar.
     * This shows the provided [count] number inside of a squircle if lower than 100, otherwise it will
     * show an infinity symbol inside of the same squircle shape.
     *
     * @property count The number of tabs to display in the tab counter.
     * @property contentDescription The content description for this button.
     * @property showPrivacyMask Whether ot not to decorate this button with a top right icon
     * signaling that the tabs are private.
     * @property onClick [BrowserToolbarEvent] to be dispatched when this button is clicked.
     * @property onLongClick Optional [BrowserToolbarInteraction] describing how to handle this button
     * being long clicked.
     */
    data class TabCounterAction(
        val count: Int,
        val contentDescription: String,
        val showPrivacyMask: Boolean,
        val onClick: BrowserToolbarEvent,
        val onLongClick: BrowserToolbarInteraction? = null,
    ) : Action()

    /**
     * A transient pill-shaped action that shows an [iconResId] with a [textResId] label, then animates
     * away into regular action button. Optionally displays an [overlayResId] icon at the bottom-end.
     *
     * @property iconResId The resource ID of the main icon to display.
     * @property overlayResId The resource ID of a smaller optional icon overlaid at the bottom-end of [iconResId].
     * @property textResId The resource ID of the label shown briefly beside the icon before it fades.
     * @property contentDescriptionResId The resource ID of the accessibility description for the button.
     * @property highlighted Whether to highlight this button.
     * @property onClick [BrowserToolbarInteraction] dispatched when the button is tapped.
     */
    data class AnimatedPillActionRes(
        @param:DrawableRes val iconResId: Int,
        @param:DrawableRes val overlayResId: Int,
        @param:StringRes val textResId: Int,
        @param:StringRes val contentDescriptionResId: Int,
        val highlighted: Boolean = false,
        val onClick: BrowserToolbarInteraction,
    ) : Action()
}
