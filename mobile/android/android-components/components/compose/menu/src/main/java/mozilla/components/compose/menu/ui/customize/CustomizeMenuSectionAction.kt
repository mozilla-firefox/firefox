/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import mozilla.components.compose.base.text.value
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemsGroup

/**
 * An action moving a section of the customize menu screen, offered to accessibility services.
 *
 * @property label What the action is called.
 * @property movedGroups The groups as moved by the action, or `null` if it cannot move the section now.
 * @property announcement What to tell the user once the action moved the section.
 */
internal data class CustomizeMenuSectionAction(
    val label: String,
    val movedGroups: CustomizeMenuGroups?,
    val announcement: String,
)

/** The actions moving [group], a group of [groups]: up, down and hide for a shown group, unhide for a hidden one. */
@Composable
internal fun customizeMenuSectionActions(
    group: MenuItemsGroup,
    groups: CustomizeMenuGroups,
): List<CustomizeMenuSectionAction> {
    val shortName = group.name?.value ?: group.itemNames()
    val shownIndex = groups.shown.indexOfFirst { it.id == group.id }
    val shownCount = groups.shown.size
    return if (shownIndex != -1) {
        listOf(
            CustomizeMenuSectionAction(
                label = stringResource(R.string.mozac_menu_customize_move_up),
                movedGroups = groups.moveUp(group.id),
                announcement =
                    stringResource(R.string.mozac_menu_customize_section_moved, shortName, shownIndex, shownCount),
            ),
            CustomizeMenuSectionAction(
                label = stringResource(R.string.mozac_menu_customize_move_down),
                movedGroups = groups.moveDown(group.id),
                announcement =
                    stringResource(R.string.mozac_menu_customize_section_moved, shortName, shownIndex + 2, shownCount),
            ),
            CustomizeMenuSectionAction(
                label = stringResource(R.string.mozac_menu_customize_hide),
                movedGroups = groups.moveToHidden(group.id),
                announcement = stringResource(R.string.mozac_menu_customize_section_hidden, shortName),
            ),
        )
    } else {
        listOf(
            CustomizeMenuSectionAction(
                label = stringResource(R.string.mozac_menu_customize_unhide),
                movedGroups = groups.moveToShown(group.id),
                announcement = stringResource(R.string.mozac_menu_customize_section_shown, shortName),
            )
        )
    }
}
