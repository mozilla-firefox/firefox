/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

/**
 * The items of a customize menu screen list which the user can drag to reorder them, hide them or show them again: the
 * shown ones above [CustomizeMenuItemKey.HIDDEN_TITLE] and the hidden ones below it.
 */
internal interface HideableListItems {
    /** Keys of the hidden items, in the order they are shown in. */
    val hiddenKeys: List<Any>

    /** Index in the list of [CustomizeMenuItemKey.HIDDEN_TITLE]. */
    val hiddenTitleIndex: Int

    /** Whether the list item with [key] is shown, above [CustomizeMenuItemKey.HIDDEN_TITLE]. */
    fun isShownKey(key: Any): Boolean

    /** Whether the item with [key] cannot be hidden. */
    fun cannotBeHidden(key: Any): Boolean

    /**
     * These items with the one with [key] moved to [target], never hiding one which [cannotBeHidden]. Equal to these
     * items if nothing moves, which is how a drop back in place is told apart.
     */
    fun drop(key: Any, target: CustomizeMenuDropTarget): HideableListItems
}
