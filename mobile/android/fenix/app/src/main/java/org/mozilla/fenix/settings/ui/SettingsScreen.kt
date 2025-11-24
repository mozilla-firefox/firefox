/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.lib.state.ext.observeAsState
import org.mozilla.fenix.settings.navigation.SettingsDestination
import org.mozilla.fenix.settings.store.SettingsAction.DestinationChanged
import org.mozilla.fenix.settings.store.SettingsAction.ItemClicked
import org.mozilla.fenix.settings.store.SettingsAction.ItemToggled
import org.mozilla.fenix.settings.store.SettingsItem
import org.mozilla.fenix.settings.store.SettingsState
import org.mozilla.fenix.settings.store.SettingsStore
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The main settings screen.
 *
 * @param store The [SettingsStore] backing the settings functionality.
 */
@Composable
fun SettingsScreen(
    destination: SettingsDestination,
    store: SettingsStore,
) {
    val state by store.observeAsState(initialValue = store.state) { it }

    LaunchedEffect(destination) {
        store.dispatch(DestinationChanged(destination))
    }

    SettingsContent(
        state = state,
        onItemClick = { item ->
            store.dispatch(ItemClicked(item))
        },
        onToggleChange = { preferenceKey, newValue ->
            store.dispatch(ItemToggled(preferenceKey, newValue))
        },
    )
}

/**
 * The content of the settings screen.
 */
@Composable
private fun SettingsContent(
    state: SettingsState,
    onItemClick: (SettingsItem) -> Unit,
    onToggleChange: (Int, Boolean) -> Unit,
) {
    Box(
        modifier = Modifier
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
                itemsIndexed(
                    items = state.settingsItems,
                    key = { _, item -> item.title.resId },
                ) { index, item ->
                    if (item.isVisible) {
                        val hasPreviousVisibleItem =
                            (0 until index).any { state.settingsItems[it].isVisible }

                        if (hasPreviousVisibleItem && item is SettingsItem.Category) {
                            HorizontalDivider(
                                color = MaterialTheme.colorScheme.onSurface,
                            )
                        }

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
}

@Composable
@PreviewLightDark
private fun SettingsScreenPreview() {
    FirefoxTheme {
        val previewState = SettingsState(
            isLoading = false,
            settingsItems = listOf(
                SettingsItem.Category(
                    title = TextValue.fromRes(android.R.string.untitled),
                ),
                SettingsItem.SimplePreference(
                    title = TextValue.fromRes(android.R.string.search_go),
                    summary = TextValue.fromRes(android.R.string.ok),
                ),
                SettingsItem.TogglePreference(
                    title = TextValue.fromRes(android.R.string.paste),
                    summary = TextValue.fromRes(android.R.string.ok),
                    isChecked = true,
                    preferenceKey = android.R.string.copy,
                ),
            ),
        )

        SettingsContent(
            state = previewState,
            onItemClick = {},
            onToggleChange = { _, _ -> },
        )
    }
}
