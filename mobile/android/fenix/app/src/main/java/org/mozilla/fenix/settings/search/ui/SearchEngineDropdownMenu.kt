/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.search.ui

import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Overflow button that opens a menu to edit or delete the search engine.
 *
 * @param isCustomSearchEngine Whether the "Edit" item is shown.
 * @param allowDeletion Whether the "Delete" item is shown.
 * @param onEditClick Invoked when the "Edit" item is clicked.
 * @param onDeleteClick Invoked when the "Delete" item is clicked.
 */
@Composable
internal fun SearchEngineDropdownMenu(
    isCustomSearchEngine: Boolean,
    allowDeletion: Boolean,
    onEditClick: () -> Unit,
    onDeleteClick: () -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }

    SearchEngineDropdownMenu(
        menuItems = searchEngineMenuItems(isCustomSearchEngine, allowDeletion, onEditClick, onDeleteClick),
        expanded = expanded,
        onExpandedChange = { expanded = it },
    )
}

@Composable
private fun SearchEngineDropdownMenu(
    menuItems: List<MenuItem>,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
) {
    Box {
        IconButton(
            onClick = { onExpandedChange(true) },
            contentDescription = stringResource(R.string.content_description_menu),
            modifier = Modifier.testTag(SearchEngineDropdownMenuTestTag.OVERFLOW_BUTTON),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_more_vertical_round_24),
                contentDescription = null,
            )
        }

        DropdownMenu(
            menuItems = menuItems,
            expanded = expanded,
            modifier =
                Modifier.semantics {
                    testTagsAsResourceId = true
                },
            onDismissRequest = { onExpandedChange(false) },
        )
    }
}

@VisibleForTesting
internal fun searchEngineMenuItems(
    isCustomSearchEngine: Boolean,
    allowDeletion: Boolean,
    onEditClick: () -> Unit,
    onDeleteClick: () -> Unit,
): List<MenuItem> = buildList {
    if (isCustomSearchEngine) {
        add(
            MenuItem.TextItem(
                text = Text.Resource(R.string.search_engine_edit),
                testTag = SearchEngineDropdownMenuTestTag.EDIT,
                onClick = onEditClick,
            )
        )
    }

    if (allowDeletion) {
        add(
            MenuItem.TextItem(
                text = Text.Resource(R.string.search_engine_delete),
                level = MenuItem.FixedItem.Level.Critical,
                onClick = onDeleteClick,
            )
        )
    }
}

@PreviewLightDark
@Composable
private fun SearchEngineDropdownMenuPreview() {
    FirefoxTheme {
        Surface {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                SearchEngineDropdownMenu(
                    menuItems =
                        searchEngineMenuItems(
                            isCustomSearchEngine = true,
                            allowDeletion = true,
                            onEditClick = {},
                            onDeleteClick = {},
                        ),
                    expanded = true,
                    onExpandedChange = {},
                )
            }
        }
    }
}
