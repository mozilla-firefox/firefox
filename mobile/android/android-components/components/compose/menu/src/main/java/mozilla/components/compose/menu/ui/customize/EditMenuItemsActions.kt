/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

/**
 * What the rows of the screen customizing the items of a menu section do for accessibility services.
 *
 * @property items The items of the screen.
 * @property onExpansionToggle Called with the id of an expandable item when the user expands or collapses it.
 * @property announce Called with what to tell the user, politely, once a custom action was done.
 */
internal class EditMenuItemsActions(
    val items: CustomizeMenuItems,
    val onExpansionToggle: (itemId: String) -> Unit,
    val announce: (announcement: String) -> Unit,
)
