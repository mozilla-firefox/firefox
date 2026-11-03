/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
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
 * A non-scrollable container for menu items inside a customize menu screen.
 *
 * @param menuGroups The groups of menu items to display.
 * @param modifier [Modifier] to be applied to the menu container.
 * @param mandatoryGroupIds Ids of the [menuGroups] which cannot be removed from the menu.
 * @param isHidden Whether these groups were removed from the menu.
 */
@Composable
fun CustomizeMenu(
    menuGroups: List<MenuItemsGroup>,
    modifier: Modifier = Modifier,
    mandatoryGroupIds: Set<String> = emptySet(),
    isHidden: Boolean = false,
) {
    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(
                    top = AcornTheme.layout.space.static100,
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static25,
                    bottom = AcornTheme.layout.space.static300,
                ),
        verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static150),
    ) {
        menuGroups.forEach { group ->
            key(group.id) {
                CustomizeMenuGroupContent(
                    group = group,
                    state =
                        when {
                            isHidden -> CustomizeMenuGroupState.HIDDEN
                            group.id in mandatoryGroupIds -> CustomizeMenuGroupState.MANDATORY
                            else -> CustomizeMenuGroupState.SHOWN
                        },
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuPreview() {
    AcornTheme {
        Surface {
            CustomizeMenu(
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
