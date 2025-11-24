/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import mozilla.components.lib.state.ext.observeAsComposableState
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.store.SettingsAction.ItemClicked
import org.mozilla.fenix.settings.store.SettingsAction.ItemToggled
import org.mozilla.fenix.settings.store.SettingsItem
import org.mozilla.fenix.settings.store.SettingsState
import org.mozilla.fenix.settings.store.SettingsStore
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Screen showing settings search results.
 *
 * @param store The [SettingsStore] backing the settings functionality.
 */
@Composable
fun SearchSettingsScreen(
    store: SettingsStore,
) {
    val state by store.observeAsComposableState { it }

    if (state.filteredItems.isEmpty()) {
        EmptySearchResultsView()
    } else {
        SearchResults(
            state = state,
            onItemClick = { item ->
                store.dispatch(ItemClicked(item))
            },
            onToggleChange = { preferenceKey, newValue ->
                store.dispatch(ItemToggled(preferenceKey, newValue))
            },
        )
    }
}

/**
 * The content of the settings screen.
 */
@Composable
private fun SearchResults(
    state: SettingsState,
    onItemClick: (SettingsItem) -> Unit,
    onToggleChange: (Int, Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.surface),
    ) {
        if (state.isLoading) {
            CircularProgressIndicator(
                modifier = Modifier.align(Alignment.Center),
                color = MaterialTheme.colorScheme.onSurface,
            )
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxSize(),
            ) {
                items(state.filteredItems.filter { it.isVisible }) { item ->
                    item.render(
                        onItemClick = onItemClick,
                        onToggleChange = { preferenceKey, newValue ->
                            onToggleChange(preferenceKey, newValue)
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun EmptySearchResultsView(
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier,
        contentAlignment = Alignment.Center,
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Image(
                modifier = Modifier.size(77.dp),
                painter = painterResource(R.drawable.fox_exclamation_alert),
                contentDescription = null,
            )

            Text(
                text = stringResource(R.string.settings_search_no_results_title),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.headline7,
            )
            Text(
                text = stringResource(R.string.settings_search_no_results_message),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.body2,
            )
        }
    }
}
