/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.background
import androidx.compose.foundation.basicMarquee
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInWindow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.draggable.detectListPressAndDrag
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON
import mozilla.components.compose.menu.ui.utils.customizeMenuPreviewData
import mozilla.components.lib.state.ext.observeAsComposableState

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
    val windowWidth = LocalWindowInfo.current.containerSize.width
    // How far right of the center of the window this is, when beside a larger notch than navigation bar for example.
    var offCenter by remember { mutableFloatStateOf(0f) }
    // The list takes the whole width, to be scrolled from anywhere, with its items no wider than the maximum width.
    BoxWithConstraints(
        modifier =
            modifier.fillMaxSize().background(MaterialTheme.colorScheme.surfaceContainer).onGloballyPositioned {
                offCenter = it.positionInWindow().x + (it.size.width - windowWidth) / 2f
            }
    ) {
        val freeWidth =
            if (constraints.hasBoundedWidth) {
                (maxWidth - AcornTheme.layout.size.containerMaxWidth).coerceAtLeast(0.dp)
            } else {
                0.dp
            }
        // The items are centered in the window, as far as the free width allows.
        val shift = with(LocalDensity.current) { offCenter.toDp() }.coerceIn(-freeWidth / 2, freeWidth / 2)
        CustomizeMenuList(
            store = store,
            leftPadding = freeWidth / 2 - shift,
            rightPadding = freeWidth / 2 + shift,
            onResetToDefault = onResetToDefault,
            onPreviewClick = onPreviewClick,
        )
    }
}

@Composable
private fun CustomizeMenuList(
    store: CustomizableMenuStore,
    leftPadding: Dp,
    rightPadding: Dp,
    onResetToDefault: () -> Unit,
    onPreviewClick: () -> Unit,
) {
    val menuState by store.observeAsComposableState { it }
    val listState = rememberLazyListState()
    val drag = rememberCustomizeMenuDragState(store, menuState, listState)
    val itemSpacing = AcornTheme.layout.space.static150

    LazyColumn(
        modifier =
            Modifier.fillMaxSize()
                .onGloballyPositioned(drag.hiddenArea::onListPositioned)
                .detectListPressAndDrag(
                    listState = listState,
                    interactionState = drag.interactionState,
                    shouldLongPressToDrag = true,
                ),
        state = listState,
        userScrollEnabled = !drag.isDragging,
        contentPadding =
            PaddingValues.Absolute(
                left = leftPadding,
                right = rightPadding,
                bottom = AcornTheme.layout.space.static300,
            ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        item(key = DRAG_BAR) {
            CustomizeMenuDragBar(onPreviewClick)
        }

        customizeMenuGroups(menuGroups = drag.groups.shown, drag = drag, firstIndex = 1, itemSpacing = itemSpacing)

        item(key = HIDDEN_SECTIONS) {
            val isDropTarget by
                remember(drag.dropTarget) {
                    derivedStateOf { drag.dropTarget.value == CustomizeMenuDropTarget.EmptyHiddenArea }
                }
            CustomizeMenuHiddenSections(
                hasHiddenItems = drag.hasHiddenGroups,
                isDropTarget = isDropTarget,
                onTitlePositioned = drag.hiddenArea::onTitlePositioned,
                onPositioned = drag.hiddenArea::onHiddenSectionsPositioned,
                modifier =
                    Modifier.animateItem()
                        .padding(top = itemSpacing)
                        .dropLine(HIDDEN_SECTIONS, drag.dropTarget, itemSpacing),
            )
        }

        customizeMenuGroups(
            menuGroups = drag.groups.hidden,
            drag = drag,
            firstIndex = drag.firstHiddenIndex,
            itemSpacing = itemSpacing,
            isHidden = true,
        )

        if (menuState.isCustomized) {
            item(key = RESET_BUTTON) {
                CustomizeMenuResetButton(
                    onClick = onResetToDefault,
                    modifier =
                        Modifier.animateItem()
                            .padding(top = itemSpacing)
                            .onGloballyPositioned(drag.hiddenArea::onResetButtonPositioned),
                )
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
                    start = AcornTheme.layout.space.static200,
                    top = AcornTheme.layout.space.static300 + AcornTheme.layout.space.static150,
                    end = AcornTheme.layout.space.static200,
                    bottom = 0.dp,
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
private fun CustomizeMenuHiddenSections(
    hasHiddenItems: Boolean,
    isDropTarget: Boolean,
    onTitlePositioned: (LayoutCoordinates) -> Unit,
    onPositioned: (LayoutCoordinates) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier.onGloballyPositioned(onPositioned)) {
        Text(
            text = stringResource(R.string.mozac_menu_customize_hidden_sections),
            modifier =
                Modifier.onGloballyPositioned(onTitlePositioned)
                    .fillMaxWidth()
                    .defaultMinSize(minHeight = AcornTheme.layout.space.static600)
                    .padding(
                        start = AcornTheme.layout.space.static200,
                        end = AcornTheme.layout.space.static100,
                        top = AcornTheme.layout.space.static100,
                        bottom = AcornTheme.layout.space.static50,
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
                            top = AcornTheme.layout.space.static150,
                            end = AcornTheme.layout.space.static200,
                        )
                        .then(
                            if (isDropTarget) {
                                Modifier.background(
                                    color = MaterialTheme.colorScheme.tertiaryContainer,
                                    shape = MaterialTheme.shapes.extraLarge,
                                )
                            } else {
                                Modifier
                            }
                        )
                        .hiddenSectionBorder(isHighlighted = isDropTarget)
                        .defaultMinSize(minHeight = 72.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = stringResource(R.string.mozac_menu_customize_drag_to_hide),
                    style = AcornTheme.typography.body2,
                    color =
                        if (isDropTarget) {
                            MaterialTheme.colorScheme.tertiary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuScreenWithTabsShownPreview() {
    AcornTheme {
        CustomizeMenuContent(
            CustomizableMenuStore(
                initialState =
                    CustomizableMenuState(
                        menuGroups = customizeMenuPreviewData,
                        isCustomized = false,
                    )
            )
        )
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuScreenWithTabsHiddenPreview() {
    AcornTheme {
        CustomizeMenuContent(
            CustomizableMenuStore(
                initialState =
                    CustomizableMenuState(
                        menuGroups = customizeMenuPreviewData,
                        hiddenGroupIds = setOf("tabs"),
                        isCustomized = true,
                    )
            )
        )
    }
}
