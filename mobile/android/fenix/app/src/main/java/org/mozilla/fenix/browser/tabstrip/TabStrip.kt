/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import androidx.compose.ui.graphics.Color

import android.annotation.SuppressLint
import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.traversalIndex
import androidx.compose.ui.tooling.preview.Devices
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.coerceIn
import androidx.compose.ui.unit.dp
import androidx.core.text.BidiFormatter
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.feature.tabs.TabsUseCases
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.HorizontalFadingEdgeBox
import org.mozilla.fenix.compose.ext.isItemPartiallyVisible
import org.mozilla.fenix.tabstray.browser.compose.DragItemContainer
import org.mozilla.fenix.tabstray.browser.compose.createListReorderState
import org.mozilla.fenix.tabstray.browser.compose.detectListPressAndDrag
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.theme.ThemedValue
import org.mozilla.fenix.theme.ThemedValueProvider
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.GleanMetrics.TabStrip as TabStripMetrics
import androidx.compose.ui.text.style.TextOverflow
import mozilla.components.compose.base.theme.AcornTheme

private val minTabStripItemWidth = 400.dp
private val maxTabStripItemWidth = 400.dp
private val tabItemHeight = 72.dp// не влияет
private val tabStripIconSize = 48.dp// не влияет
private val spaceBetweenTabs = 12.dp
private val tabStripListContentStartPadding = 12.dp
private val titleFadeWidth = 0.dp // нам не нужен
private val tabStripHorizontalPadding = 20.dp // от начала табов и в конце табов

/**
 * Top level composable for the tabs strip.
 *
 * @param isSelectDisabled Whether or not the tabs can be shown as selected.
 * @param showActionButtons Show the action buttons in the tabs strip when true.
 * @param tabStripColors The colors to use for the tabs strip.
 * @param browserStore The [BrowserStore] instance used to observe tabs state.
 * @param appStore The [AppStore] instance used to observe browsing mode.
 * @param tabsUseCases The [TabsUseCases] instance to perform tab actions.
 * @param onAddTabClick Invoked when the add tab button is clicked.
 * @param onCloseTabClick Invoked when a tab is closed.
 * @param onLastTabClose Invoked when the last remaining open tab is closed.
 * @param onSelectedTabClick Invoked when a tab is selected.
 * @param onTabCounterClick Invoked when tab counter is clicked.
 */
@Composable
fun TabStrip(
    isSelectDisabled: Boolean = false,
    showActionButtons: Boolean = true,
    tabStripColors: TabStripColors = TabStripColors.default(),
    browserStore: BrowserStore = components.core.store,
    appStore: AppStore = components.appStore,
    tabsUseCases: TabsUseCases = components.useCases.tabsUseCases,
    onAddTabClick: () -> Unit,
    onCloseTabClick: (isPrivate: Boolean) -> Unit,
    onLastTabClose: (isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (url: String) -> Unit,
    onTabCounterClick: () -> Unit,
) {
    val isPossiblyPrivateMode by remember { appStore.stateFlow.map { it.mode.isPrivate } }
        .collectAsState(initial = false)
    val state by remember {
        browserStore.stateFlow.map {
            it.toTabStripState(
                isSelectDisabled = isSelectDisabled,
                isPossiblyPrivateMode = isPossiblyPrivateMode,
                addTab = onAddTabClick,
                closeTab = { isPrivate, numberOfTabs ->
                    it.selectedTabId?.let { selectedTabId ->
                        closeTab(
                            numberOfTabs = numberOfTabs,
                            isPrivate = isPrivate,
                            tabsUseCases = tabsUseCases,
                            tabId = selectedTabId,
                            onLastTabClose = onLastTabClose,
                            onCloseTabClick = onCloseTabClick,
                        )
                    }
                },
            )
        }
    }.collectAsState(initial = TabStripState.initial)

    TabStripContent(
        state = state,
        showActionButtons = showActionButtons,
        colors = tabStripColors,
        onAddTabClick = {
            onAddTabClick()
            TabStripMetrics.newTabTapped.record()
        },
        onCloseTabClick = { tabId, isPrivate ->
            closeTab(
                numberOfTabs = state.tabs.size,
                isPrivate = isPrivate,
                tabsUseCases = tabsUseCases,
                tabId = tabId,
                onLastTabClose = onLastTabClose,
                onCloseTabClick = onCloseTabClick,
            )
        },
        onSelectedTabClick = { tabId, url ->
            tabsUseCases.selectTab(tabId)
            onSelectedTabClick(url)
            TabStripMetrics.selectTab.record()
        },
        onMove = { tabId, targetId, placeAfter ->
            if (tabId != targetId) {
                tabsUseCases.moveTabs(listOf(tabId), targetId, placeAfter)
            }
        },
        onTabCounterClick = onTabCounterClick,
    )
}

@Composable
private fun TabStripContent(
    state: TabStripState,
    colors: TabStripColors,
    showActionButtons: Boolean = true,
    onAddTabClick: () -> Unit,
    onCloseTabClick: (id: String, isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (tabId: String, url: String) -> Unit,
    onMove: (tabId: String, targetId: String, placeAfter: Boolean) -> Unit,
    onTabCounterClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(dimensionResource(R.dimen.tab_strip_height))
            .background(colors.backgroundColor)
            .systemGestureExclusion()
            .padding(start = tabStripListContentStartPadding, end = tabStripListContentStartPadding, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Row(
            modifier = Modifier.weight(1f, fill = false),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TabsList(
                state = state,
                modifier = Modifier.weight(1f, fill = false),
                tabItemBackgroundColors = colors.tabItemBackgroundColors,
                onCloseTabClick = onCloseTabClick,
                onSelectedTabClick = onSelectedTabClick,
                onMove = onMove,
            )

            if (showActionButtons) {
                // Modified: Set IconButton size to 72 dp
                IconButton(
                    onClick = onAddTabClick,
                    modifier = Modifier.size(12.dp)
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_plus_24),
                        tint = MaterialTheme.colorScheme.onSurface,
                        contentDescription = stringResource(R.string.add_tab),
                        modifier = Modifier.size(LocalResources.current.getDimensionPixelSize(com.android.car.navioui.R.dimen.dsDefaultHeight).dp),
                        //tint = Color.Unspecified
                    )
                }
            }
        }

        if (showActionButtons) {
            TabStripTabCounterButton(
                tabCount = state.tabs.size,
                size = dimensionResource(R.dimen.tab_strip_height),
                menuItems = state.menuItems,
                privacyBadgeVisible = state.isPrivateMode,
                onClick = onTabCounterClick,
                modifier = Modifier.padding(end = spaceBetweenTabs)
            )
        }
    }
}

// There is a bug with `BoxWithConstraints` where it flags the `BoxWithConstraintsScope` being unused
// even though it's being used implicitly below via the `maxWidth` property of `BoxWithConstraintsScope`.
@SuppressLint("UnusedBoxWithConstraintsScope")
@Composable
private fun TabsList(
    state: TabStripState,
    tabItemBackgroundColors: TabStripColors.TabColors,
    modifier: Modifier = Modifier,
    onCloseTabClick: (id: String, isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (tabId: String, url: String) -> Unit,
    onMove: (tabId: String, targetId: String, placeAfter: Boolean) -> Unit,
) {
    BoxWithConstraints(modifier = modifier) {
        val listState = rememberLazyListState()
        val tabWidth = calculateTabWidth(state.tabs.size)

        val reorderState = createListReorderState(
            listState = listState,
            onMove = { movedTab, adjacentTab ->
                onMove(
                    (movedTab.key as String),
                    (adjacentTab.key as String),
                    movedTab.index < adjacentTab.index,
                )
            },
            ignoredItems = emptyList(),
        )

        LazyRow(
            modifier = Modifier
                .detectListPressAndDrag(
                    reorderState = reorderState,
                    listState = listState,
                    shouldLongPressToDrag = true,
                )
                .selectableGroup(),
            state = listState,
        ) {
            itemsIndexed(
                items = state.tabs,
                key = { _, item -> item.id },
            ) { index, itemState ->
                DragItemContainer(
                    state = reorderState,
                    key = itemState.id,
                    position = index,
                ) {
                    TabItem(
                        state = itemState,
                        onCloseTabClick = onCloseTabClick,
                        onSelectedTabClick = onSelectedTabClick,
                        backgroundColors = tabItemBackgroundColors,
                        modifier = Modifier
                            .padding(end = spaceBetweenTabs)
                            .animateItem()
                            .width(tabWidth)
                            .thenConditional(
                                modifier = Modifier.semantics { traversalIndex = -1f },
                                predicate = { itemState.isSelected },
                            ),
                    )
                }
            }
        }

        if (state.tabs.isNotEmpty()) {
            // When a new tab is added, scroll to the end of the list. This is done here instead of
            // in onCloseTabClick so this acts on state change which can occur from any other
            // place e.g. tabs tray.
            LaunchedEffect(state.tabs.last().id) {
                listState.scrollToItem(state.tabs.size)
            }

            // When a tab is selected, scroll to the selected tab. This is done here instead of
            // in onSelectedTabClick so this acts on state change which can occur from any other
            // place e.g. tabs tray.
            val selectedTab = state.tabs.firstOrNull { it.isSelected }
            LaunchedEffect(selectedTab?.id) {
                if (selectedTab != null) {
                    val selectedItemInfo =
                        listState.layoutInfo.visibleItemsInfo.firstOrNull { it.key == selectedTab.id }

                    if (selectedItemInfo == null || listState.isItemPartiallyVisible(selectedItemInfo)) {
                        listState.animateScrollToItem(state.tabs.indexOf(selectedTab))
                    }
                }
            }
        }
    }
}

/**
 * Calculates the width of each tab item based on available width and the number of tabs.
 *
 * @param tabCount The number of tabs to display.
 * @return The calculated width for each tab, constrained between min and max tab widths.
 */
@Composable
private fun BoxWithConstraintsScope.calculateTabWidth(tabCount: Int): Dp {
    val availableWidth = maxWidth - tabStripListContentStartPadding
    return ((availableWidth / tabCount) - spaceBetweenTabs).coerceIn(
        minimumValue = minTabStripItemWidth,
        maximumValue = maxTabStripItemWidth,
    )
}

@Composable
@Suppress("LongMethod", "CognitiveComplexMethod")
private fun TabItem(
    state: TabStripItem,
    modifier: Modifier = Modifier,
    backgroundColors: TabStripColors.TabColors,
    onCloseTabClick: (id: String, isPrivate: Boolean) -> Unit,
    onSelectedTabClick: (id: String, url: String) -> Unit,
) {
    val backgroundColor = backgroundColors.get(state.isSelected)
    val closeTabLabel = stringResource(R.string.close_tab)

    TabStripCard(
        modifier = modifier.height(tabItemHeight).width(400.dp),
        backgroundColor = backgroundColor,
        elevation = if (state.isSelected) {
            selectedTabStripCardElevation
        } else {
            defaultTabStripCardElevation
        },
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .clickable { onSelectedTabClick(state.id, state.url) }
                .semantics {
                    role = Role.Tab
                    selected = state.isSelected
                    customActions = listOf(
                        CustomAccessibilityAction(
                            label = closeTabLabel,
                        ) {
                            onCloseTabClick(state.id, state.isPrivate)
                            true
                        },
                    )
                },
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // This makes sure that isRtl is only calculated when the title changes.
                val isTitleRtl = remember(state.title) {
                    BidiFormatter.getInstance().isRtl(state.title)
                }

                HorizontalFadingEdgeBox(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(),
                    fadeWidth = titleFadeWidth,
                    backgroundColor = backgroundColor,
                    isContentRtl = isTitleRtl,
                ) {
                    Text(
                        text = state.title,
                        modifier = Modifier.align(Alignment.CenterStart)
                            .padding(start = 28.dp),
                        color = if (state.isSelected) {
                            AcornTheme.colors.textInverse
                        } else {
                            AcornTheme.colors.textSecondary
                        },
                        softWrap = false,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = FirefoxTheme.typography.subtitle2,
                    )
                }
            }

            if (state.isCloseButtonVisible) {
                IconButton(
                    onClick = { onCloseTabClick(state.id, state.isPrivate) },
                    modifier =  Modifier.size(
                        LocalResources.current.getDimensionPixelSize(com.android.car.navioui.R.dimen.dsDefaultHeight).dp)
                    ,
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_cross_20),
                        tint = if (state.isSelected) {
                            AcornTheme.colors.iconInverse
                        } else {
                            AcornTheme.colors.iconSecondary
                        },
                        modifier = Modifier.size(36.dp),
                        contentDescription = stringResource(
                            id = R.string.close_tab_title,
                            state.title,
                        ),
                    )
                }
            } else {
                Spacer(modifier = Modifier.size(8.dp))
            }
        }
    }
}

/**
 * Displays the icon for a tab in the tab strip.
 *
 * @param url The URL of the tab, used to generate a favicon if no icon is provided.
 * @param icon The tab's favicon bitmap, if available.
 */
@Composable
private fun TabStripIcon(
    url: String,
    icon: Bitmap?,
) {
    Box(
        modifier = Modifier
            .size(tabStripIconSize)
            .clip(CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        if (icon != null && !icon.isRecycled) {
            Image(
                bitmap = icon.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier
                    .size(tabStripIconSize)
                    .clip(CircleShape),
            )
        } else if (url == ABOUT_HOME_URL) {
            Favicon(
                imageResource = R.drawable.ic_firefox,
                size = tabStripIconSize,
            )
        } else {
            Favicon(
                url = url,
                size = tabStripIconSize,
            )
        }
    }
}

private fun closeTab(
    numberOfTabs: Int,
    isPrivate: Boolean,
    tabsUseCases: TabsUseCases,
    tabId: String,
    onLastTabClose: (isPrivate: Boolean) -> Unit,
    onCloseTabClick: (isPrivate: Boolean) -> Unit,
) {
    if (numberOfTabs == 1) {
        onLastTabClose(isPrivate)
    }
    tabsUseCases.removeTab(tabId)
    onCloseTabClick(isPrivate)
    TabStripMetrics.closeTab.record()
}

private class TabUIStateParameterProvider : ThemedValueProvider<TabStripState>(
    sequenceOf(
        TabStripState(
            listOf(
                TabStripItem(
                    id = "1",
                    title = "Tab 1",
                    url = "https://www.mozilla.org",
                    isPrivate = false,
                    isSelected = false,
                ),
                TabStripItem(
                    id = "2",
                    title = "Tab 2 with a very long title that should be truncated",
                    url = "https://www.mozilla.org",
                    isPrivate = false,
                    isSelected = false,
                ),
                TabStripItem(
                    id = "3",
                    title = "Selected tab",
                    url = "https://www.mozilla.org",
                    isPrivate = false,
                    isSelected = true,
                ),
                TabStripItem(
                    id = "p1",
                    title = "Private tab 1",
                    url = "https://www.mozilla.org",
                    isPrivate = true,
                    isSelected = false,
                ),
                TabStripItem(
                    id = "p2",
                    title = "Private selected tab",
                    url = "https://www.mozilla.org",
                    isPrivate = true,
                    isSelected = true,
                ),
            ),
            isPrivateMode = false,
            tabCounterMenuItems = emptyList(),
        ),
    ),
)

@Preview(device = Devices.PIXEL_TABLET)
@Composable
private fun TabStripPreview(
    @PreviewParameter(TabUIStateParameterProvider::class) tabStripState: ThemedValue<TabStripState>,
) {
    FirefoxTheme(tabStripState.theme) {
        TabStripContentPreview(
            tabStripState.value.tabs.filter {
                if (tabStripState.theme == Theme.Private) {
                    it.isPrivate
                } else {
                    !it.isPrivate
                }
            },
        )
    }
}

@Composable
private fun TabStripContentPreview(tabs: List<TabStripItem>) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(dimensionResource(id = R.dimen.tab_strip_height)),
        contentAlignment = Alignment.Center,
    ) {
        TabStripContent(
            state = TabStripState(
                tabs = tabs,
                isPrivateMode = false,
                tabCounterMenuItems = emptyList(),
            ),
            colors = TabStripColors.default(),
            onAddTabClick = {},
            onCloseTabClick = { _, _ -> },
            onSelectedTabClick = { _, _ -> },
            onMove = { _, _, _ -> },
            onTabCounterClick = {},
        )
    }
}

@Preview(device = Devices.PIXEL_TABLET)
@Composable
private fun TabStripPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    val browserStore = BrowserStore()

    FirefoxTheme(theme) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(dimensionResource(id = R.dimen.tab_strip_height)),
            contentAlignment = Alignment.Center,
        ) {
            TabStrip(
                appStore = AppStore(),
                browserStore = browserStore,
                tabsUseCases = TabsUseCases(browserStore),
                onAddTabClick = {
                    val tab = createTab(
                        url = "www.example.com",
                    )
                    browserStore.dispatch(TabListAction.AddTabAction(tab))
                },
                onLastTabClose = {},
                onCloseTabClick = {},
                onSelectedTabClick = {},
                onTabCounterClick = {},
            )
        }
    }
}
