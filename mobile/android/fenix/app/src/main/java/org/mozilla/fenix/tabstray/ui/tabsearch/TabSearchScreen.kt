/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabsearch

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.rememberSearchBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.searchbar.TopSearchBar
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.toShortUrl
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.TabsTrayTestTag.TAB_SEARCH_BACK_BUTTON
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.redux.action.TabSearchAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.middleware.TabSearchMiddleware
import org.mozilla.fenix.tabstray.redux.state.TabSearchState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.tabstray.ui.tabitems.BasicTabListItem
import org.mozilla.fenix.tabstray.ui.tabpage.EmptyTabPage
import org.mozilla.fenix.theme.FirefoxTheme

private val SearchResultsCornerRadius = 12.dp
private val SearchResultsPadding = 16.dp

/**
 * The top-level Composable for the Tab Search feature within the Tab Manager.
 *
 * @param state The current snapshot of [TabSearchState].
 * @param onAction Invoked to pass upwards a [TabsTrayAction] in response to a UI event.
 */
@Composable
fun TabSearchScreen(
    state: TabSearchState,
    onAction: (TabsTrayAction) -> Unit,
) {
    val searchBarState = rememberSearchBarState()
    var expanded by remember { mutableStateOf(false) }
    val focusRequester = remember { FocusRequester() }
    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current
    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }

    Scaffold(
        topBar = {
            TopSearchBar(
                state = searchBarState,
                modifier = Modifier.focusRequester(focusRequester).padding(horizontal = 8.dp),
                query = state.query,
                onQueryChange = { onAction(TabSearchAction.SearchQueryChanged(it)) },
                onSearch = { submitted ->
                    onAction(TabSearchAction.SearchQueryChanged(submitted))
                    focusManager.clearFocus()
                    keyboardController?.hide()
                },
                expanded = expanded,
                onExpandedChange = { expanded = it },
                placeholder = {
                    Text(stringResource(id = R.string.tab_manager_search_bar_placeholder))
                },
                leadingIcon = {
                    IconButton(
                        onClick = {
                            expanded = false
                            focusManager.clearFocus(force = true)
                            keyboardController?.hide()
                            onAction(TabsTrayAction.NavigateBackInvoked)
                        },
                        contentDescription =
                            stringResource(id = R.string.tab_manager_search_bar_back_content_description),
                        modifier = Modifier.testTag(TAB_SEARCH_BACK_BUTTON),
                    ) {
                        Icon(
                            painter = painterResource(id = iconsR.drawable.mozac_ic_back_24),
                            contentDescription = null,
                        )
                    }
                },
            )
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier.padding(innerPadding).fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (state.showNoResults) {
                EmptyTabSearchResults(modifier = Modifier.fillMaxSize())
            } else {
                TabSearchResults(
                    searchResults = state.searchResults,
                    query = state.query,
                    modifier = Modifier.padding(horizontal = SearchResultsPadding),
                    onSearchResultClicked = { onAction(TabSearchAction.SearchResultClicked(it)) },
                )
            }
        }
    }
}

/**
 * Composable for the tab search screen results.
 *
 * @param searchResults List of search results.
 * @param query The current search query the user has entered.
 * @param modifier The [Modifier] to be applied.
 * @param onSearchResultClicked Invoked when a search result item is clicked.
 */
@Composable
private fun TabSearchResults(
    searchResults: List<TabsTrayItem>,
    query: String,
    modifier: Modifier = Modifier,
    onSearchResultClicked: (TabsTrayItem) -> Unit,
) {
    val listState = rememberLazyListState()

    LaunchedEffect(query) {
        if (listState.firstVisibleItemIndex != 0 || listState.firstVisibleItemScrollOffset != 0) {
            listState.scrollToItem(0)
        }
    }

    val lastIndex = searchResults.lastIndex

    LazyColumn(
        state = listState,
        modifier = modifier.fillMaxWidth(),
        contentPadding = PaddingValues(vertical = SearchResultsPadding),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        itemsIndexed(
            items = searchResults,
            key = { _, tab -> tab.id },
        ) { index, tabItem ->
            val itemShape =
                when {
                    lastIndex == 0 -> RoundedCornerShape(SearchResultsCornerRadius)
                    index == 0 ->
                        RoundedCornerShape(
                            topStart = SearchResultsCornerRadius,
                            topEnd = SearchResultsCornerRadius,
                        )
                    index == lastIndex ->
                        RoundedCornerShape(
                            bottomStart = SearchResultsCornerRadius,
                            bottomEnd = SearchResultsCornerRadius,
                        )
                    else -> RoundedCornerShape(0.dp)
                }

            when (tabItem) {
                is TabsTrayItem.Tab ->
                    TabItemSearchResult(
                        tab = tabItem,
                        shape = itemShape,
                        onSearchResultClicked = onSearchResultClicked,
                    )
                is TabsTrayItem.TabGroup -> {}
            }

            if (index < lastIndex) {
                HorizontalDivider(modifier = Modifier.widthIn(max = FirefoxTheme.layout.size.containerMaxWidth))
            }
        }
    }
}

@Composable
private fun TabItemSearchResult(
    tab: TabsTrayItem.Tab,
    shape: Shape,
    onSearchResultClicked: (TabsTrayItem) -> Unit,
) {
    val tabUrl = tab.url.toShortUrl()
    val faviconPainter =
        tab.icon?.run {
            prepareToDraw()
            BitmapPainter(asImageBitmap())
        }

    BasicTabListItem(
        title = tab.title,
        url = tabUrl,
        modifier =
            Modifier.clip(shape)
                .widthIn(max = FirefoxTheme.layout.size.containerMaxWidth)
                .background(MaterialTheme.colorScheme.surfaceContainerLowest)
                .testTag(tag = TabsTrayTestTag.TAB_ITEM_ROOT),
        faviconPainter = faviconPainter,
        onClick = { onSearchResultClicked(tab) },
    )
}

/**
 * Composable for the tab search screen when there are no results.
 *
 * @param modifier The [Modifier] to be applied.
 */
@Composable
private fun EmptyTabSearchResults(modifier: Modifier = Modifier) {
    EmptyTabPage(modifier = modifier) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Image(
                modifier = Modifier.size(77.dp),
                painter = painterResource(R.drawable.fox_exclamation_alert),
                contentDescription = null,
            )

            Text(
                text = stringResource(R.string.tab_manager_no_search_results),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.body2,
            )

            Text(
                text = stringResource(R.string.tab_manager_no_search_results_additional_text),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.body2,
            )
        }
    }
}

private class TabSearchParameterProvider : PreviewParameterProvider<TabsTrayState> {
    private val searchResults =
        listOf(
            createTab(
                url = "mozilla.org",
                id = "1",
                title = "Mozilla",
            ),
            createTab(
                url = "maps.google.com",
                id = "2",
                title = "Google Maps",
            ),
            createTab(
                url = "google.com/maps/place/Mozilla+Toronto/@43.6472856,-79.3944129,17z/",
                id = "3",
                title = "Long Google Maps URL",
            ),
        )

    private val manySearchResults = buildList {
        repeat(4) { index ->
            searchResults.forEach { tab -> add(tab.copy(id = "${tab.id}-$index")) }
        }
    }

    override val values =
        sequenceOf(
            TabsTrayState(),
            TabsTrayState(
                tabSearchState =
                    TabSearchState(
                        query = "m",
                        searchResults = searchResults,
                    )
            ),
            TabsTrayState(
                tabSearchState =
                    TabSearchState(
                        query = "firefox",
                        searchResults = emptyList(),
                    )
            ),
            TabsTrayState(
                tabSearchState =
                    TabSearchState(
                        query = "m",
                        searchResults = manySearchResults,
                    )
            ),
        )
}

/** Preview for the tab search screen. */
@FlexibleWindowLightDarkPreview
@Composable
private fun TabSearchScreenPreview(@PreviewParameter(TabSearchParameterProvider::class) state: TabsTrayState) {
    val scope = rememberCoroutineScope()
    val store = remember {
        TabsTrayStore(
            initialState = state,
            middlewares = listOf(TabSearchMiddleware(scope = scope)),
        )
    }

    FirefoxTheme {
        TabSearchScreen(
            state = store.state.tabSearchState,
            onAction = store::dispatch,
        )
    }
}
