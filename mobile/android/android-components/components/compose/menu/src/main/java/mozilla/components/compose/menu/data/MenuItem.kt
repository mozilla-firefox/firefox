/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.data

import androidx.annotation.DrawableRes
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.Role.Companion.Button
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIcon
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.compose.menu.ui.MenuItemState.DEFAULT

/** Parent of all items that can be shown in a menu. */
sealed class MenuItem {
    abstract val title: MenuItemTitle
    abstract val contentDescription: Text?
    abstract val onClickEvent: MenuEvent?
    abstract val role: Role
    abstract val summary: MenuItemSummary?
    abstract val icon: MenuItemIcon?
    abstract val showNewIndicator: Boolean
    abstract val badge: MenuItemBadge?
    abstract val state: MenuItemState
    abstract val onLongClickEvent: MenuEvent?
}

/**
 * Configuration of menu item that can be expanded to show others or collapsed to hide them.
 *
 * @param title The title of the menu item.
 * @param onClickEvent [MenuEvent] to dispatch when the menu item is clicked as a side effect. Clicking this menu item
 *   will automatically expand or collapse its [subMenuItems]
 * @param subMenuItems List of [StandardMenuItem] to show when this item is expanded.
 * @param hideOnExpand Whether to automatically hide the menu item when it is expanded so that only its [subMenuItems]
 *   will remain displayed. This also means that once expanded the list of [subMenuItems] cannot be collapsed again.
 * @param role The [Role] of the menu item.
 * @param contentDescription Optional custom content description for the menu item.
 * @param summary An optional summary of the menu item.
 * @param icon An optional icon of the menu item.
 * @param showNewIndicator Whether to show a new indicator.
 * @param badge An optional badge to show.
 * @param onLongClickEvent [MenuEvent] to dispatch when the menu item is long-clicked.
 * @param actionButtonText An optional text to show in the expanded button indicator.
 * @param state The state of this menu item.
 */
data class ExpandableMenuItem(
    override val title: MenuItemTitle,
    override val onClickEvent: MenuEvent,
    val subMenuItems: List<StandardMenuItem>,
    val hideOnExpand: Boolean = false,
    override val role: Role = Button,
    override val contentDescription: Text? = null,
    override val summary: MenuItemSummary? = null,
    override val icon: MenuItemIcon? = null,
    override val showNewIndicator: Boolean = false,
    override val badge: MenuItemBadge? = null,
    override val onLongClickEvent: MenuEvent? = null,
    val actionButtonText: Text? = null,
    override val state: MenuItemState = DEFAULT,
) : MenuItem()

/**
 * Configuration of a standard menu item.
 *
 * @param title The title of the menu item.
 * @param onClickEvent An optional [MenuEvent] to dispatch when the menu item is clicked.
 * @param onShownEvent An optional [MenuEvent] to dispatch when the menu item is shown.
 * @param contentDescription Optional custom content description for the menu item.
 * @param role The [Role] of the menu item.
 * @param summary An optional summary of the menu item.
 * @param icon An optional icon of the menu item.
 * @param showNewIndicator Whether to show a new indicator.
 * @param badge An optional badge to show. For a [Role.Switch] this is expected to show the current state, which screen
 *   readers will announce whenever it changes.
 * @param onLongClickEvent [MenuEvent] to dispatch when the menu item is long-clicked.
 * @param actionButton An optional action button to show.
 * @param trailingIcon An optional icon to show at the end of the menu item, as a hint of what clicking it does.
 * @param state The state of this menu item.
 */
data class StandardMenuItem(
    override val title: MenuItemTitle,
    override val onClickEvent: MenuEvent? = null,
    val onShownEvent: MenuEvent? = null,
    override val contentDescription: Text? = null,
    override val role: Role = Button,
    override val summary: MenuItemSummary? = null,
    override val icon: MenuItemIcon? = null,
    override val showNewIndicator: Boolean = false,
    override val badge: MenuItemBadge? = null,
    override val onLongClickEvent: MenuEvent? = null,
    val actionButton: MenuItemActionButton? = null,
    @DrawableRes val trailingIcon: Int? = null,
    override val state: MenuItemState = DEFAULT,
) : MenuItem()

/**
 * Configuration of a full-width promotional banner that is user dismissable. Only shown in list groups as grids don't
 * have the room for it.
 *
 * @param title The title of the banner.
 * @param summary Summary to show for this banner.
 * @param icon Icon to show for this banner.
 * @param onClickEvent [MenuEvent] to dispatch when the banner is clicked.
 * @param onDismissEvent [MenuEvent] to dispatch when the banner is dismissed.
 */
data class BannerMenuItem(
    override val title: MenuItemTitle,
    override val summary: MenuItemSummary,
    override val icon: MenuItemIcon,
    override val onClickEvent: MenuEvent,
    val onDismissEvent: MenuEvent,
) : MenuItem() {
    override val contentDescription: Text? = null
    override val role: Role = Button
    override val showNewIndicator: Boolean = false
    override val badge: MenuItemBadge? = null
    override val state: MenuItemState = DEFAULT
    override val onLongClickEvent: MenuEvent? = null
}

/**
 * Configuration of the title text of a [MenuItem].
 *
 * @param text The text to show.
 * @param maxLines The maximum number of lines to show this text on.
 */
data class MenuItemTitle(
    val text: Text,
    val maxLines: Int = 2,
)

/**
 * Configuration of the summary text of a [MenuItem].
 *
 * @param text The text to show.
 * @param state The [MenuItemState] of this summary.
 * @param maxLines The maximum number of lines to show this text on.
 */
data class MenuItemSummary(
    val text: Text,
    val state: MenuItemState = DEFAULT,
    val maxLines: Int = 1,
)

/**
 * Configuration of a badge to shown in a [MenuItem].
 *
 * @param text The text to show.
 * @param state The [MenuItemState] of this badge.
 */
data class MenuItemBadge(
    val text: Text,
    val state: MenuItemState = DEFAULT,
)

/**
 * Configuration of an action button to shown in a [MenuItem].
 *
 * @param icon The icon to show.
 * @param contentDescription The content description of this button.
 * @param onClickEvent The [MenuEvent] to dispatch when this button is clicked.
 * @param showDivider Whether to show a divider before this button.
 * @param state The [MenuItemState] of this action button.
 */
data class MenuItemActionButton(
    @DrawableRes val icon: Int,
    val contentDescription: Text,
    val onClickEvent: MenuEvent,
    val showDivider: Boolean = false,
    val state: MenuItemState = DEFAULT,
)
