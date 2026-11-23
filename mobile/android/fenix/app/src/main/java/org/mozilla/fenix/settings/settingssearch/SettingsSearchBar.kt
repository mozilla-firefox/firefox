/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.settingssearch

import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import mozilla.components.compose.base.button.IconButton
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Composable for the settings search bar.
 *
 * @param store [SettingsSearchStore] for the screen.
 * @param onBackClick Invoked when the app bar's back button is clicked.
 * @param isSearchFocused Whether the search bar is currently focused.
 * @param onSearchFocusChange Invoked when the search bar's focus state changes.
 */
@Composable
fun SettingsSearchBar(
    store: SettingsSearchStore,
    onBackClick: () -> Unit,
    isSearchFocused: Boolean,
    onSearchFocusChange: (Boolean) -> Unit,
) {
    val focusRequester = remember { FocusRequester() }

    TopAppBar(
        modifier = Modifier.wrapContentHeight(),
        title = {
            SettingsSearchField(
                store = store,
                focusRequester = focusRequester,
                isSearchFocused = isSearchFocused,
                onSearchFocusChange = onSearchFocusChange,
            )
        },
        navigationIcon = { BackButton(onClick = onBackClick) },
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@Composable
private fun SettingsSearchField(
    store: SettingsSearchStore,
    focusRequester: FocusRequester,
    isSearchFocused: Boolean,
    onSearchFocusChange: (Boolean) -> Unit,
) {
    val state by store.observeAsComposableState { it }
    val lifecycleOwner = LocalLifecycleOwner.current

    var searchQuery by remember {
        mutableStateOf(
            TextFieldValue(
                text = state.searchQuery,
                selection = TextRange(state.searchQuery.length),
            )
        )
    }

    TextField(
        value = searchQuery,
        onValueChange = { value: TextFieldValue ->
            searchQuery = value
            store.dispatch(SettingsSearchAction.SearchQueryUpdated(value.text))
        },
        textStyle = FirefoxTheme.typography.body1,
        modifier =
            Modifier.fillMaxWidth().focusRequester(focusRequester).onFocusChanged { focusState ->
                if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
                    onSearchFocusChange(focusState.isFocused)
                }
            },
        placeholder =
            @Composable {
                Text(
                    text = stringResource(R.string.settings_search_title),
                    style = FirefoxTheme.typography.body1,
                )
            },
        singleLine = true,
        colors =
            TextFieldDefaults.colors(
                focusedContainerColor = Color.Transparent,
                unfocusedContainerColor = Color.Transparent,
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent,
                errorIndicatorColor = Color.Transparent,
            ),
        trailingIcon =
            @Composable {
                when (state) {
                    is SettingsSearchState.SearchInProgress,
                    is SettingsSearchState.NoSearchResults -> {
                        ClearTextButton(
                            onClick = {
                                searchQuery = TextFieldValue("")
                                store.dispatch(SettingsSearchAction.SearchQueryUpdated(""))
                            }
                        )
                    }
                    else -> Unit
                }
            },
    )

    LaunchedEffect(Unit) {
        if (isSearchFocused) {
            focusRequester.requestFocus()
        }
    }
}

@Composable
private fun BackButton(onClick: () -> Unit) {
    IconButton(
        onClick = onClick,
        contentDescription = stringResource(R.string.content_description_settings_search_navigate_back),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_back_24),
            contentDescription = null,
        )
    }
}

@Composable
private fun ClearTextButton(onClick: () -> Unit) {
    IconButton(
        onClick = onClick,
        contentDescription = stringResource(R.string.content_description_settings_search_clear_search),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_cross_circle_fill_24),
            contentDescription = null,
        )
    }
}
