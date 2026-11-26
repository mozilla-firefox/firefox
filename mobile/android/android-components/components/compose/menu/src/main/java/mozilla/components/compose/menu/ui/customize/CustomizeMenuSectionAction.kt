/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import mozilla.components.compose.base.text.value
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.ui.icons.R as iconsR

/**
 * The actions moving [group], a group of [groups]: up, down and hide for a shown group, up and down, which cannot
 * apply, and unhide for a hidden one.
 */
@Composable
internal fun customizeMenuSectionActions(
    group: MenuItemsGroup,
    groups: CustomizeMenuGroups,
): List<CustomizeMenuAction<CustomizeMenuGroups>> {
    val shortName = group.name?.value ?: group.itemNames()
    val shownIndex = groups.shown.indexOfFirst { it.id == group.id }
    val shownCount = groups.shown.size
    return if (shownIndex != -1) {
        listOf(
            CustomizeMenuAction(
                label = stringResource(R.string.mozac_menu_customize_move_up),
                icon = iconsR.drawable.mozac_ic_chevron_up_24,
                result = groups.moveUp(group.id),
                announcement =
                    stringResource(R.string.mozac_menu_customize_section_moved, shortName, shownIndex, shownCount),
            ),
            CustomizeMenuAction(
                label = stringResource(R.string.mozac_menu_customize_move_down),
                icon = iconsR.drawable.mozac_ic_chevron_down_24,
                result = groups.moveDown(group.id),
                announcement =
                    stringResource(R.string.mozac_menu_customize_section_moved, shortName, shownIndex + 2, shownCount),
            ),
            CustomizeMenuAction(
                label = stringResource(R.string.mozac_menu_customize_hide),
                icon = iconsR.drawable.mozac_ic_eye_slash_24,
                result = groups.moveToHidden(group.id),
                announcement = stringResource(R.string.mozac_menu_customize_section_hidden, shortName),
            ),
        )
    } else {
        listOf(
            CustomizeMenuAction(
                label = stringResource(R.string.mozac_menu_customize_move_up),
                icon = iconsR.drawable.mozac_ic_chevron_up_24,
                result = null,
                announcement = "",
            ),
            CustomizeMenuAction(
                label = stringResource(R.string.mozac_menu_customize_move_down),
                icon = iconsR.drawable.mozac_ic_chevron_down_24,
                result = null,
                announcement = "",
            ),
            CustomizeMenuAction(
                label = stringResource(R.string.mozac_menu_customize_unhide),
                icon = iconsR.drawable.mozac_ic_eye_24,
                result = groups.moveToShown(group.id),
                announcement = stringResource(R.string.mozac_menu_customize_section_shown, shortName),
            ),
        )
    }
}
