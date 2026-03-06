/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.searchbar

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SearchBarColors
import androidx.compose.material3.SearchBarDefaults
import androidx.compose.material3.SearchBarScrollBehavior
import androidx.compose.material3.SearchBarState
import androidx.compose.material3.Text
import androidx.compose.material3.rememberSearchBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.R
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.theme.AcornTheme
import androidx.compose.material3.SearchBar as M3SearchBar
import androidx.compose.material3.TopSearchBar as M3TopSearchBar
import mozilla.components.ui.icons.R as iconsR

/**
 * A wrapper around the Material3 [SearchBar] that exposes a query-based API and uses
 * [SearchBarInputField] to handle input behavior. This version adds support for dynamic
 * trailing-icon behavior, allowing icons to update based on the current query state.
 *
 * @param query the current text entered in the search field.
 * @param onQueryChange called when the query text is changed by the user.
 * @param onSearch called when the user submits the current [query] (e.g. IME action search).
 * @param expanded whether this search bar is expanded and showing search results.
 * @param onExpandedChange called when this search bar's expanded state changes.
 * @param modifier the [Modifier] to be applied to this search bar.
 * @param enabled whether the search bar is enabled.
 * @param placeholder optional placeholder composable displayed in the input field when [query] is empty.
 * @param leadingIcon optional leading icon displayed at the start of the input field.
 * @param trailingIcon trailing icon displayed at the end of the input field when there is
 * no text or when a clear icon is not being shown.
 * @param shape the shape of this search bar when it is not expanded. When expanded, the shape will
 * always be [SearchBarDefaults.fullScreenShape].
 * @param colors [SearchBarColors] used to resolve the colors for this search bar in different
 * states. See [SearchBarDefaults.colors].
 * @param tonalElevation when [SearchBarColors.containerColor] is surface, a translucent
 * primary color overlay is applied on top of the container. A higher tonal elevation
 * value will result in a darker color in light theme and a lighter color in dark theme.
 * @param shadowElevation the elevation for the shadow below this search bar.
 * @param windowInsets the window insets that this search bar will respect.
 * @param interactionSource the [MutableInteractionSource] representing the stream of interactions
 * for this search bar. You can use this to observe focus, press, and other interactions.
 * @param content the content of this search bar to display search results below the input field
 * when [expanded] is true.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SearchBar(
    query: String,
    onQueryChange: (String) -> Unit,
    onSearch: (String) -> Unit,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
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
    content: @Composable ColumnScope.() -> Unit,
) {
    M3SearchBar(
        inputField = {
            SearchBarInputField(
                query = query,
                onQueryChange = onQueryChange,
                onSearch = onSearch,
                expanded = expanded,
                onExpandedChange = onExpandedChange,
                enabled = enabled,
                placeholder = placeholder,
                leadingIcon = leadingIcon,
                trailingIcon = trailingIcon,
                colors = colors,
                interactionSource = interactionSource,
            )
        },
        expanded = expanded,
        onExpandedChange = onExpandedChange,
        modifier = modifier,
        shape = shape,
        colors = colors,
        tonalElevation = tonalElevation,
        shadowElevation = shadowElevation,
        windowInsets = windowInsets,
        content = content,
    )
}

/**
 * A Material3 [SearchBar] that places the search bar at the top of the layout and uses
 * [SearchBarInputField] to handle input behavior. This adds support for dynamic
 * trailing-icon behavior and automatically updates icons based on the current query state.
 *
 * @param state the state of the search bar.
 * @param modifier optional [Modifier] for customizing the layout or behavior of the search bar.
 * @param query the current query text displayed in the input field.
 * @param onQueryChange invoked when the query text changes.
 * @param onSearch invoked when the user submits the current query.
 * @param expanded whether the search bar is currently expanded and showing search results.
 * @param onExpandedChange invoked when the expanded state changes.
 * @param enabled whether the search bar is interactive.
 * @param placeholder optional placeholder composable shown when the query is empty.
 * @param leadingIcon optional composable displayed at the start of the input field.
 * @param trailingIcon trailing icon displayed when the bar is not expanded, or when
 * the implementation determines no dynamic icon should replace it.
 * @param shape the shape of the search bar when collapsed; expanded shape is always
 * [SearchBarDefaults.fullScreenShape].
 * @param colors the [SearchBarColors] used to resolve container, content, and indicator colors.
 * @param tonalElevation the tonal elevation applied to the container surface.
 * @param shadowElevation the elevation of the search bar's shadow.
 * @param windowInsets the window insets that this search bar should respect.
 * @param interactionSource an optional [MutableInteractionSource] for observing focus and
 * press interactions within the input field.
 * @param scrollBehavior optional [SearchBarScrollBehavior] controlling how the search bar
 * responds to scroll events (pinned, enter-always, etc.).
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
    M3TopSearchBar(
        state = state,
        inputField = {
            SearchBarInputField(
                query = query,
                onQueryChange = onQueryChange,
                onSearch = onSearch,
                expanded = expanded,
                onExpandedChange = onExpandedChange,
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
        colors = colors,
        tonalElevation = tonalElevation,
        shadowElevation = shadowElevation,
        windowInsets = windowInsets,
        scrollBehavior = scrollBehavior,
    )
}

@Suppress("LongParameterList")
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SearchBarInputField(
    query: String,
    onQueryChange: (String) -> Unit,
    onSearch: (String) -> Unit,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    enabled: Boolean,
    placeholder: @Composable (() -> Unit)?,
    leadingIcon: @Composable (() -> Unit)?,
    trailingIcon: (@Composable () -> Unit)?,
    colors: SearchBarColors,
    interactionSource: MutableInteractionSource?,
) {
    CompositionLocalProvider(
        LocalTextStyle provides AcornTheme.typography.body1,
    ) {
        SearchBarDefaults.InputField(
            modifier = Modifier.fillMaxWidth(),
            query = query,
            onQueryChange = onQueryChange,
            onSearch = onSearch,
            expanded = expanded,
            onExpandedChange = onExpandedChange,
            enabled = enabled,
            placeholder = placeholder,
            leadingIcon = leadingIcon,
            trailingIcon = {
                CompositionLocalProvider(
                    LocalContentColor provides MaterialTheme.colorScheme.onSurface,
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        when {
                            !expanded -> trailingIcon?.invoke()
                            expanded && query.isEmpty() -> Unit
                            expanded && query.isNotEmpty() -> {
                                IconButton(onClick = { onQueryChange("") }) {
                                    Icon(
                                        painterResource(iconsR.drawable.mozac_ic_cross_circle_fill_24),
                                        contentDescription = stringResource(
                                            R.string.text_field_cross_trailing_icon_default_content_description,
                                        ),
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

/**
 * A preview state for the SearchBar.
 *
 * @param showTrailing Whether or not to show trailing icon.
 * @param showSecondaryTrailing Whether or not to show secondary trailing icon.
 */
private data class SearchBarPreviewState(
    val showTrailing: Boolean = false,
    val showSecondaryTrailing: Boolean = false,
)

/**
 * Provides a sequence of [SearchBarPreviewState] configurations for Compose previews to render
 * the SearchBar in multiple visual states without duplicating preview functions.
 */
private class SearchBarIconsPreviewProvider :
    PreviewParameterProvider<SearchBarPreviewState> {
    override val values = sequenceOf(
        SearchBarPreviewState(),
        SearchBarPreviewState(
            showTrailing = true,
        ),
        SearchBarPreviewState(
            showTrailing = true,
            showSecondaryTrailing = true,
        ),
    )
}

private val PreviewSearchItems = listOf(
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

@OptIn(ExperimentalMaterial3Api::class)
@PreviewLightDark
@Composable
private fun SearchBarPreview(
    @PreviewParameter(SearchBarIconsPreviewProvider::class)
    previewState: SearchBarPreviewState,
) {
    var query by remember { mutableStateOf("") }
    var expanded by remember { mutableStateOf(false) }
    val filteredItems = rememberFilteredItems(query)

    AcornTheme {
        SearchBar(
            query = query,
            onQueryChange = { query = it },
            onSearch = {},
            expanded = expanded,
            onExpandedChange = { expanded = it },
            placeholder = { Text("Search...") },
            leadingIcon = {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_search_24),
                    contentDescription = "content description",
                )
            },
            trailingIcon =
                if (false) {
                    {
                        Row {
                            if (previewState.showTrailing) {
                                IconButton(
                                    onClick = {},
                                ) {
                                    Icon(
                                        painter = painterResource(id = iconsR.drawable.mozac_ic_qr_code_24),
                                        contentDescription = "content description",
                                    )
                                }
                            }
                            if (previewState.showSecondaryTrailing) {
                                IconButton(
                                    onClick = {},
                                ) {
                                    Icon(
                                        painter = painterResource(id = iconsR.drawable.mozac_ic_microphone_24),
                                        contentDescription = "content description",
                                    )
                                }
                            }
                        }
                    }
                } else {
                    null
                },
        ) {
            CompositionLocalProvider(
                LocalContentColor provides MaterialTheme.colorScheme.onSurface,
            ) {
                filteredItems.forEach { item ->
                    Text(
                        text = item,
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
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
                        ) {
                            Icon(
                                painter = painterResource(id = iconsR.drawable.mozac_ic_search_24),
                                contentDescription = "content description",
                            )
                        }
                    },
                    trailingIcon = {
                        null
//                        Row {
//                            IconButton(
//                                onClick = {},
//                            ) {
//                                Icon(
//                                    painter = painterResource(id = iconsR.drawable.mozac_ic_qr_code_24),
//                                    contentDescription = "content description",
//                                )
//                            }
//                            IconButton(
//                                onClick = {},
//                            ) {
//                                Icon(
//                                    painter = painterResource(id = iconsR.drawable.mozac_ic_microphone_24),
//                                    contentDescription = "content description",
//                                )
//                            }
//                        }
                    },
                )
            },
        ) { innerPadding ->
            LazyColumn(
                modifier = Modifier
                    .padding(innerPadding),
            ) {
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
