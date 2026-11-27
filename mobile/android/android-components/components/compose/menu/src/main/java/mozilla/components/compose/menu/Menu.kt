/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.MenuAttribution
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.compose.menu.ui.ListMenuItemsGroup
import mozilla.components.compose.menu.ui.MenuAttribution
import mozilla.components.compose.menu.ui.MenuGridContainer
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.utils.MenuPreviewParameterProvider
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.ui.icons.R as iconsR

/**
 * A vertically scrollable container for menu items.
 *
 * @param store The [MenuStore] backing this menu.
 * @param modifier [Modifier] to be applied to the menu container.
 * @param onHeaderDarkBackgroundChanged Optional callback invoked when the sticky header's dark background state
 *   changes.
 */
@Suppress("CognitiveComplexMethod")
@Composable
fun Menu(
    store: MenuStore,
    modifier: Modifier = Modifier,
    onHeaderDarkBackgroundChanged: ((Boolean) -> Unit)? = null,
) {
    Surface(
        modifier = modifier,
        color = MaterialTheme.colorScheme.surfaceContainer,
    ) {
        val onInteraction: (MenuEvent) -> Unit = remember(store) { { store.dispatch(it) } }
        val menuState by store.observeAsComposableState { it }
        val menuGroups = menuState.menuGroups
        val attribution = menuState.attribution

        val headerGroup = menuGroups.firstOrNull()?.takeIf { it.isSticky }
        val footerGroup = menuGroups.lastOrNull()?.takeIf { menuGroups.size > 1 && it.isSticky }
        val scrollableGroups =
            menuGroups.drop(if (headerGroup != null) 1 else 0).dropLast(if (footerGroup != null) 1 else 0)

        val listState = rememberLazyListState()

        val isScrollable = listState.canScrollForward || listState.canScrollBackward
        val isHeaderDark = headerGroup != null && isScrollable

        LaunchedEffect(isHeaderDark) {
            onHeaderDarkBackgroundChanged?.invoke(isHeaderDark)
        }

        val stickyBackgroundColor =
            if (isScrollable) {
                MaterialTheme.colorScheme.surfaceContainerHigh
            } else {
                MaterialTheme.colorScheme.surfaceContainer
            }

        Column {
            if (attribution?.showAtTop == true) {
                MenuAttribution(attribution = attribution)
            }

            if (headerGroup != null) {
                MenuHeader(
                    headerGroup = headerGroup,
                    showDivider = listState.canScrollBackward,
                    onInteraction = onInteraction,
                    backgroundColor = stickyBackgroundColor,
                )
            }

            MenuContent(
                listState = listState,
                scrollableGroups = scrollableGroups,
                onInteraction = onInteraction,
                topPadding =
                    if (headerGroup == null || scrollableGroups.firstOrNull()?.isBanner() == true) {
                        AcornTheme.layout.space.static100
                    } else {
                        0.dp
                    },
                bottomPadding = if (footerGroup != null) 0.dp else AcornTheme.layout.space.static100,
                modifier = Modifier.weight(1f, fill = false),
            )

            if (footerGroup != null) {
                MenuFooter(
                    footerGroup = footerGroup,
                    showDivider = listState.canScrollForward,
                    onInteraction = onInteraction,
                    backgroundColor = stickyBackgroundColor,
                )
            }

            if (attribution?.showAtTop == false) {
                MenuAttribution(attribution = attribution)
            }
        }
    }
}

private fun MenuItemsGroup.isBanner() = this is MenuItemsGroup.Row && items.singleOrNull() is BannerMenuItem

@Composable
private fun MenuContent(
    listState: LazyListState,
    scrollableGroups: List<MenuItemsGroup>,
    onInteraction: (MenuEvent) -> Unit,
    topPadding: Dp,
    bottomPadding: Dp,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        state = listState,
        modifier = modifier.fillMaxWidth(),
        contentPadding =
            PaddingValues(
                top = topPadding,
                bottom = bottomPadding,
            ),
        verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static150),
    ) {
        scrollableGroups.forEach { group ->
            item(key = group.id) {
                MenuGroupContent(
                    group,
                    onInteraction,
                    Modifier.padding(horizontal = AcornTheme.layout.space.static100),
                )
            }
        }
    }
}

@Composable
private fun MenuGroupContent(
    group: MenuItemsGroup,
    onInteraction: (MenuEvent) -> Unit,
    modifier: Modifier = Modifier,
    isSticky: Boolean = false,
    backgroundColor: Color = Color.Transparent,
) {
    when (group) {
        is MenuItemsGroup.Grid -> {
            MenuGridContainer(
                items = group.items,
                onInteraction = onInteraction,
                modifier = modifier,
                isSticky = isSticky,
                showTitles = group.showTitles,
                backgroundColor = backgroundColor,
            )
        }

        is MenuItemsGroup.Row -> {
            ListMenuItemsGroup(group.items, onInteraction, modifier)
        }
    }
}

/**
 * The group staying at the top of the menu while the others scroll.
 *
 * This is kept out of the scrolling list, as a sticky header of it would keep the menu busy forever if one of its items
 * gets focused.
 */
@Composable
private fun MenuHeader(
    headerGroup: MenuItemsGroup,
    showDivider: Boolean,
    onInteraction: (MenuEvent) -> Unit,
    backgroundColor: Color,
) {
    Column(modifier = Modifier.background(backgroundColor).padding(top = AcornTheme.layout.space.static150)) {
        MenuGroupContent(
            headerGroup,
            onInteraction,
            modifier = Modifier.padding(bottom = AcornTheme.layout.space.static100),
            isSticky = true,
            backgroundColor = backgroundColor,
        )

        if (showDivider) {
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        }
    }
}

@Composable
private fun MenuFooter(
    footerGroup: MenuItemsGroup,
    showDivider: Boolean,
    onInteraction: (MenuEvent) -> Unit,
    backgroundColor: Color,
) {
    Column(modifier = Modifier.background(backgroundColor).padding(bottom = AcornTheme.layout.space.static150)) {
        if (showDivider) {
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        }

        MenuGroupContent(
            footerGroup,
            onInteraction,
            modifier = Modifier.padding(top = AcornTheme.layout.space.static100),
            isSticky = true,
            backgroundColor = backgroundColor,
        )
    }
}

@PreviewLightDark
@Composable
private fun MenuPreview(@PreviewParameter(MenuPreviewParameterProvider::class) menuGroups: List<MenuItemsGroup>) {
    AcornTheme {
        Menu(
            store =
                MenuStore(
                    initialState =
                        MenuState(
                            menuGroups = menuGroups,
                            attribution =
                                MenuAttribution(
                                    title = Text.String("Powered by Mozilla"),
                                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_logo_firefox_24),
                                    showAtTop = false,
                                ),
                        )
                )
        )
    }
}
