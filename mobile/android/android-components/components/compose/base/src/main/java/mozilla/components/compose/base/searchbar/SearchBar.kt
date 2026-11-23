/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.searchbar

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.clearText
import androidx.compose.material3.AppBarWithSearch
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SearchBarColors
import androidx.compose.material3.SearchBarDefaults
import androidx.compose.material3.SearchBarScrollBehavior
import androidx.compose.material3.SearchBarState
import androidx.compose.material3.SearchBarValue
import androidx.compose.material3.Text
import androidx.compose.material3.rememberSearchBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.drop
import mozilla.components.compose.base.R
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.textfield.rememberSyncedTextFieldState
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R as iconsR

/**
 * A Material3 [AppBarWithSearch] that places the search bar at the top of the layout and uses [SearchBarInputField] to
 * handle input behavior. This adds support for dynamic trailing-icon behavior and automatically updates icons based on
 * the current query state.
 *
 * This exposes a query- and expanded-based API for callers driving the search bar from their own state, and bridges
 * those to the [SearchBarState] and [TextFieldState] the underlying Material3 component requires.
 *
 * @param state the state of the search bar.
 * @param modifier optional [Modifier] for customizing the layout or behavior of the search bar.
 * @param query the query text to sync into the input field.
 * @param onQueryChange invoked when the query text changes.
 * @param onSearch invoked when the user submits the current query.
 * @param expanded whether the search bar is currently expanded and showing search results.
 * @param onExpandedChange invoked when the expanded state changes.
 * @param enabled whether the search bar is interactive.
 * @param placeholder optional placeholder composable shown when the query is empty.
 * @param leadingIcon optional composable displayed at the start of the input field.
 * @param trailingIcon trailing icon displayed when the bar is not expanded, or when the implementation determines no
 *   dynamic icon should replace it.
 * @param shape the shape of the search bar when collapsed; expanded shape is always
 *   [SearchBarDefaults.fullScreenShape].
 * @param colors the [SearchBarColors] used to resolve container, content, and indicator colors.
 * @param tonalElevation the tonal elevation applied to the container surface.
 * @param shadowElevation the elevation of the search bar's shadow.
 * @param windowInsets the window insets that this search bar should respect.
 * @param interactionSource an optional [MutableInteractionSource] for observing focus and press interactions within the
 *   input field.
 * @param scrollBehavior optional [SearchBarScrollBehavior] controlling how the search bar responds to scroll events
 *   (pinned, enter-always, etc.).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TopSearchBar(
    state: SearchBarState,
    modifier: Modifier = Modifier,
    query: String,
    onQueryChange: (String) -> Unit,
    onSearch: (String) -> Unit,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    enabled: Boolean = true,
    placeholder: @Composable (() -> Unit)? = null,
    leadingIcon: @Composable (() -> Unit)? = null,
    trailingIcon: (@Composable () -> Unit)? = null,
    shape: Shape = SearchBarDefaults.inputFieldShape,
    colors: SearchBarColors = SearchBarDefaults.colors(),
    tonalElevation: Dp = SearchBarDefaults.TonalElevation,
    shadowElevation: Dp = SearchBarDefaults.ShadowElevation,
    windowInsets: WindowInsets = SearchBarDefaults.windowInsets,
    interactionSource: MutableInteractionSource? = null,
    scrollBehavior: SearchBarScrollBehavior? = null,
) {
    val textFieldState = rememberSyncedTextFieldState(query, onQueryChange)
    ExpansionSyncEffect(expanded, onExpandedChange, state)

    AppBarWithSearch(
        state = state,
        inputField = {
            SearchBarInputField(
                textFieldState = textFieldState,
                searchBarState = state,
                onSearch = onSearch,
                enabled = enabled,
                placeholder = placeholder,
                leadingIcon = leadingIcon,
                trailingIcon = trailingIcon,
                colors = colors,
                interactionSource = interactionSource,
            )
        },
        modifier = modifier,
        shape = shape,
        colors = SearchBarDefaults.appBarWithSearchColors(searchBarColors = colors),
        tonalElevation = tonalElevation,
        shadowElevation = shadowElevation,
        windowInsets = windowInsets,
        scrollBehavior = scrollBehavior,
    )
}

/**
 * Keeps [state] in sync with a caller-owned [expanded] flag: external changes drive the expand or collapse animation,
 * and state-driven changes are propagated back out through [onExpandedChange].
 */
@Composable
private fun ExpansionSyncEffect(
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    state: SearchBarState,
) {
    val latestExpanded by rememberUpdatedState(expanded)
    val latestOnExpandedChange by rememberUpdatedState(onExpandedChange)

    LaunchedEffect(expanded, state) {
        val target = if (expanded) SearchBarValue.Expanded else SearchBarValue.Collapsed
        if (state.targetValue != target) {
            if (expanded) state.animateToExpanded() else state.animateToCollapsed()
        }
    }
    LaunchedEffect(state) {
        // Drop the initial value so only genuine transitions are reported back to the caller.
        snapshotFlow { state.targetValue }
            .drop(1)
            .collect { value ->
                val isExpanded = value == SearchBarValue.Expanded
                if (isExpanded != latestExpanded) {
                    latestOnExpandedChange(isExpanded)
                }
            }
    }
}

@Suppress("LongParameterList")
@Composable
private fun SearchBarInputField(
    textFieldState: TextFieldState,
    searchBarState: SearchBarState,
    onSearch: (String) -> Unit,
    enabled: Boolean,
    placeholder: @Composable (() -> Unit)?,
    leadingIcon: @Composable (() -> Unit)?,
    trailingIcon: (@Composable () -> Unit)?,
    colors: SearchBarColors,
    interactionSource: MutableInteractionSource?,
) {
    val expanded = searchBarState.targetValue == SearchBarValue.Expanded
    val hasQuery = textFieldState.text.isNotEmpty()

    CompositionLocalProvider(LocalTextStyle provides AcornTheme.typography.body1) {
        SearchBarDefaults.InputField(
            textFieldState = textFieldState,
            searchBarState = searchBarState,
            onSearch = onSearch,
            modifier = Modifier.fillMaxWidth(),
            enabled = enabled,
            placeholder = placeholder,
            leadingIcon = leadingIcon,
            trailingIcon = {
                CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        when {
                            !expanded -> trailingIcon?.invoke()
                            !hasQuery -> Unit
                            else -> {
                                IconButton(
                                    onClick = { textFieldState.clearText() },
                                    contentDescription =
                                        stringResource(
                                            R.string.text_field_cross_trailing_icon_default_content_description
                                        ),
                                ) {
                                    Icon(
                                        painterResource(iconsR.drawable.mozac_ic_cross_circle_fill_24),
                                        contentDescription = null,
                                    )
                                }
                            }
                        }
                    }
                }
            },
            colors = colors.inputFieldColors,
            interactionSource = interactionSource,
        )
    }
}

private val PreviewSearchItems =
    listOf(
        "Mozilla",
        "Google",
        "Google Maps",
    )

@Composable
private fun rememberFilteredItems(
    query: String,
    items: List<String> = PreviewSearchItems,
): List<String> {
    return remember(query, items) {
        val trimmed = query.trim()
        if (trimmed.isEmpty()) {
            items
        } else {
            items.filter { it.contains(trimmed, ignoreCase = true) }
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun TopSearchBarPreview() {
    val state = rememberSearchBarState()
    var query by remember { mutableStateOf("") }
    var expanded by remember { mutableStateOf(false) }
    val filteredItems = rememberFilteredItems(query)

    AcornTheme {
        Scaffold(
            topBar = {
                TopSearchBar(
                    state = state,
                    modifier = Modifier,
                    query = query,
                    onQueryChange = { query = it },
                    onSearch = {},
                    expanded = expanded,
                    onExpandedChange = { expanded = it },
                    placeholder = { Text("Search...") },
                    leadingIcon = {
                        IconButton(
                            onClick = {},
                            contentDescription = "content description",
                        ) {
                            Icon(
                                painter = painterResource(id = iconsR.drawable.mozac_ic_search_24),
                                contentDescription = null,
                            )
                        }
                    },
                    trailingIcon = {
                        Row {
                            IconButton(
                                onClick = {},
                                contentDescription = "content description",
                            ) {
                                Icon(
                                    painter = painterResource(id = iconsR.drawable.mozac_ic_qr_code_24),
                                    contentDescription = null,
                                )
                            }
                            IconButton(
                                onClick = {},
                                contentDescription = "content description",
                            ) {
                                Icon(
                                    painter = painterResource(id = iconsR.drawable.mozac_ic_microphone_24),
                                    contentDescription = null,
                                )
                            }
                        }
                    },
                )
            }
        ) { innerPadding ->
            LazyColumn(modifier = Modifier.padding(innerPadding)) {
                items(filteredItems) { item ->
                    Text(
                        text = item,
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    )
                }
            }
        }
    }
}
