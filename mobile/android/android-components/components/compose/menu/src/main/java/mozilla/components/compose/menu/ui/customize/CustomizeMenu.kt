/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR

/**
 * Show [menuGroups] as items of a customize menu screen.
 *
 * @param menuGroups The groups of menu items to display.
 * @param mandatoryGroupIds Ids of the [menuGroups] which cannot be removed from the menu.
 * @param isHidden Whether these groups were removed from the menu.
 */
internal fun LazyListScope.customizeMenuGroups(
    menuGroups: List<MenuItemsGroup>,
    mandatoryGroupIds: Set<String> = emptySet(),
    isHidden: Boolean = false,
) {
    items(items = menuGroups, key = { it.id }) { group ->
        CustomizeMenuGroupContent(
            group = group,
            modifier =
                Modifier.padding(
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static25,
                ),
            state =
                when {
                    isHidden -> CustomizeMenuGroupState.HIDDEN
                    group.id in mandatoryGroupIds -> CustomizeMenuGroupState.MANDATORY
                    else -> CustomizeMenuGroupState.SHOWN
                },
        )
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuPreview() {
    AcornTheme {
        Surface {
            LazyColumn {
                customizeMenuGroups(
                    menuGroups =
                        listOf(
                            MenuItemsGroup.Row(
                                id = "group1",
                                items =
                                    listOf(
                                        StandardMenuItem(
                                            title = MenuItemTitle(Text.String("Bookmarks")),
                                            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_bookmark_24),
                                        ),
                                        StandardMenuItem(
                                            title = MenuItemTitle(Text.String("History")),
                                            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_history_24),
                                        ),
                                    ),
                            ),
                            MenuItemsGroup.Grid(
                                id = "group2",
                                items =
                                    listOf(
                                        StandardMenuItem(
                                            title = MenuItemTitle(Text.String("Settings")),
                                            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                                        ),
                                        StandardMenuItem(
                                            title = MenuItemTitle(Text.String("Downloads")),
                                            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_download_24),
                                        ),
                                    ),
                            ),
                        )
                )
            }
        }
    }
}
