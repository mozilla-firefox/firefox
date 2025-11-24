/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.settings.store.SettingsItem

/**
 * Renders a settings item as a Composable based on its type.
 *
 * @param onItemClick Callback for when the item is clicked.
 * @param onToggleChange Callback for when a toggle preference changes allowing to know of
 * the preference key backing the toggle and the new value.
 */
@Composable
fun SettingsItem.render(
    onItemClick: (SettingsItem) -> Unit,
    onToggleChange: (Int, Boolean) -> Unit,
) {
    when (this) {
        is SettingsItem.Category -> render(onItemClick, onToggleChange)
        is SettingsItem.SimplePreference -> render(onItemClick)
        is SettingsItem.TogglePreference -> render(onToggleChange)
    }
}

/**
 * Renders a category header and recursively renders all child items.
 *
 * @param onItemClick Callback when an item is clicked.
 * @param onToggleChange Callback for when a toggle preference changes allowing to know of
 * the preference key backing the toggle and the new value.
 */
@Composable
fun SettingsItem.Category.render(
    onItemClick: (SettingsItem) -> Unit,
    onToggleChange: (Int, Boolean) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surface),
    ) {
        Text(
            text = title.resolve(),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurface,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
        )

        items.forEach { item ->
            if (item.isVisible) {
                item.render(
                    onItemClick = onItemClick,
                    onToggleChange = onToggleChange,
                )
            }
        }
    }
}

/**
 * Renders a simple preference item (clickable, navigates to another screen).
 *
 * @param onItemClick Callback for when the item is clicked.
 */
@Composable
fun SettingsItem.SimplePreference.render(
    onItemClick: (SettingsItem) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = isEnabled) { onItemClick(this@render) }
            .background(MaterialTheme.colorScheme.surface)
            .padding(horizontal = 16.dp, vertical = 12.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(
                modifier = Modifier.weight(1f),
            ) {
                Text(
                    text = title.resolve(),
                    style = MaterialTheme.typography.bodyLarge,
                    color = if (isEnabled) {
                        MaterialTheme.colorScheme.onSurface
                    } else {
                        MaterialTheme.colorScheme.inverseOnSurface
                    },
                )
                summary?.let { summaryValue ->
                    Text(
                        text = summaryValue.resolve(),
                        style = MaterialTheme.typography.bodyMedium,
                        color = if (isEnabled) {
                            MaterialTheme.colorScheme.onSurface
                        } else {
                            MaterialTheme.colorScheme.inverseOnSurface
                        },
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
            }
        }
    }
}

/**
 * Renders a toggle preference item (switch).
 *
 * @param onToggleChange Callback for when a toggle preference changes allowing to know of
 * the preference key backing the toggle and the new value.
 */
@Composable
fun SettingsItem.TogglePreference.render(
    onToggleChange: (Int, Boolean) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surface)
            .padding(horizontal = 16.dp, vertical = 12.dp),
    ) {
        Row(
            modifier = Modifier
                .clickable {
                    onToggleChange(preferenceKey, !isChecked)
                }
                .fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(
                modifier = Modifier.weight(1f),
            ) {
                Text(
                    text = title.resolve(),
                    style = MaterialTheme.typography.bodyLarge,
                    color = if (isEnabled) {
                        MaterialTheme.colorScheme.onSurface
                    } else {
                        MaterialTheme.colorScheme.inverseOnSurface
                    },
                )
                summary?.let { summaryValue ->
                    Text(
                        text = summaryValue.resolve(),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSecondary,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
            }
            Switch(
                checked = isChecked,
                onCheckedChange = { checked ->
                    onToggleChange(preferenceKey, checked)
                },
                enabled = isEnabled,
            )
        }
    }
}
