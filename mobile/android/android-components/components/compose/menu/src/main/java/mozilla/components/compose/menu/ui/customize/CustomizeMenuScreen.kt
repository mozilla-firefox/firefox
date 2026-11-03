/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.background
import androidx.compose.foundation.basicMarquee
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.ui.icons.R as iconsR

/**
 * Content composable for the customize menu screen.
 *
 * @param store The [CustomizableMenuStore] managing the state and dispatching events for menu customization.
 * @param modifier The [Modifier] to be applied to the layout.
 * @param onResetToDefault Callback invoked when the "Reset to default" button is clicked.
 * @param onPreviewClick Callback invoked when the "Preview" button is clicked.
 */
@Composable
fun CustomizeMenuContent(
    store: CustomizableMenuStore,
    modifier: Modifier = Modifier,
    onResetToDefault: () -> Unit = {},
    onPreviewClick: () -> Unit = {},
) {
    Column(
        modifier =
            modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.surfaceContainer)
                .verticalScroll(rememberScrollState()),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Column(modifier = Modifier.widthIn(max = AcornTheme.layout.size.containerMaxWidth).fillMaxWidth()) {
            CustomizeMenuDragBar(onPreviewClick = onPreviewClick)

            val menuState by store.observeAsComposableState { it }
            val (hiddenGroups, shownGroups) =
                remember(menuState.menuGroups, menuState.hiddenGroupIds) {
                    menuState.menuGroups.partition { it.id in menuState.hiddenGroupIds }
                }

            CustomizeMenu(
                menuGroups = shownGroups,
                mandatoryGroupIds = menuState.mandatoryGroupIds,
            )

            CustomizeMenuHiddenSections(hasHiddenItems = hiddenGroups.isNotEmpty())

            if (hiddenGroups.isNotEmpty()) {
                CustomizeMenu(
                    menuGroups = hiddenGroups,
                    isHidden = true,
                )
            }

            if (menuState.isCustomized) {
                CustomizeMenuResetButton(onClick = onResetToDefault)
            }
        }
    }
}

@Composable
private fun CustomizeMenuResetButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(
                    horizontal = AcornTheme.layout.space.static200,
                    vertical = AcornTheme.layout.space.static300,
                ),
        contentAlignment = Alignment.Center,
    ) {
        OutlinedButton(
            text = stringResource(R.string.mozac_menu_customize_reset_to_default),
            onClick = onClick,
        )
    }
}

@Composable
private fun CustomizeMenuDragBar(
    onPreviewClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .defaultMinSize(minHeight = AcornTheme.layout.space.static600)
                .background(MaterialTheme.colorScheme.surfaceContainer)
                .padding(
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static100,
                ),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = stringResource(R.string.mozac_menu_customize_drag_to_reorder),
            modifier = Modifier.weight(1f).basicMarquee(),
            style = AcornTheme.typography.headline8,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
        )
        TextButton(
            text = stringResource(R.string.mozac_menu_customize_preview),
            onClick = onPreviewClick,
        )
    }
}

@Composable
private fun CustomizeMenuHiddenSections(hasHiddenItems: Boolean, modifier: Modifier = Modifier) {
    Column(modifier = modifier) {
        Text(
            text = stringResource(R.string.mozac_menu_customize_hidden_sections),
            modifier =
                Modifier.fillMaxWidth()
                    .defaultMinSize(minHeight = AcornTheme.layout.space.static600)
                    .padding(
                        start = AcornTheme.layout.space.static200,
                        end = AcornTheme.layout.space.static100,
                        top = AcornTheme.layout.space.static100,
                        bottom = AcornTheme.layout.space.static100,
                    )
                    .wrapContentHeight(Alignment.CenterVertically),
            style = AcornTheme.typography.headline8,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        if (!hasHiddenItems) {
            Box(
                modifier =
                    Modifier.fillMaxWidth()
                        .padding(
                            start = AcornTheme.layout.space.static200,
                            end = AcornTheme.layout.space.static200,
                            bottom = AcornTheme.layout.space.static300,
                        )
                        .hiddenSectionBorder()
                        .defaultMinSize(minHeight = 72.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = stringResource(R.string.mozac_menu_customize_drag_to_hide),
                    style = AcornTheme.typography.body2,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuScreenPreview() {
    AcornTheme {
        CustomizeMenuContent(
            CustomizableMenuStore(
                initialState =
                    CustomizableMenuState(
                        menuGroups =
                            listOf(
                                MenuItemsGroup.Grid(
                                    id = "navigation",
                                    items =
                                        listOf(
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("Back")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_back_24),
                                                onClickEvent = object : MenuEvent {},
                                            ),
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("Forward")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_forward_24),
                                                onClickEvent = object : MenuEvent {},
                                            ),
                                        ),
                                    isSticky = true,
                                ),
                                MenuItemsGroup.Row(
                                    id = "group1",
                                    items =
                                        listOf(
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("Bookmarks")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_star_fill_20),
                                                onClickEvent = object : MenuEvent {},
                                            )
                                        ),
                                ),
                                MenuItemsGroup.Row(
                                    id = "group2",
                                    items =
                                        listOf(
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("History")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_history_24),
                                                onClickEvent = object : MenuEvent {},
                                            )
                                        ),
                                ),
                            )
                    )
            )
        )
    }
}
