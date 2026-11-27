/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import kotlin.math.abs
import mozilla.components.compose.base.button.IconButton
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.R.string
import org.mozilla.fenix.compose.ContextualMenu
import org.mozilla.fenix.compose.MenuItem
import org.mozilla.fenix.compose.list.ExpandableListHeader
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.theme.FirefoxTheme

/** Rectangular shape with all corners rounded used to display a collapsed collection. */
private val collapsedCollectionShape: Shape
    @Composable get() = MaterialTheme.shapes.small

/**
 * Rectangular shape with only the top corners rounded used to display an expanded collection with other views placed
 * immediately below this which can be shown immediately next to it, with no visible separation.
 */
private val expandedCollectionShape: Shape
    @Composable
    get() =
        MaterialTheme.shapes.small.copy(
            bottomStart = CornerSize(0.dp),
            bottomEnd = CornerSize(0.dp),
        )

/**
 * Displays an individual [TabCollection].
 *
 * @param collection [TabCollection] to display.
 * @param expanded Whether the collection is expanded to show it's containing tabs or not.
 * @param menuItems List of [MenuItem] to be shown in a menu.
 * @param onToggleCollectionExpanded Invoked when the user clicks on the collection.
 * @param onCollectionShareTabsClicked Invoked when the user clicks to share the collection.
 */
@Composable
@Suppress("LongMethod", "Deprecation") // https://bugzilla.mozilla.org/show_bug.cgi?id=1927713
fun Collection(
    collection: TabCollection,
    expanded: Boolean,
    menuItems: List<MenuItem>,
    onToggleCollectionExpanded: (TabCollection, Boolean) -> Unit,
    onCollectionShareTabsClicked: (TabCollection) -> Unit,
) {
    var isMenuExpanded by remember(collection) { mutableStateOf(false) }
    val isExpanded by remember(collection, expanded) { mutableStateOf(expanded) }

    Card(
        modifier =
            Modifier.semantics(mergeDescendants = true) {}
                .clickable(
                    onClickLabel =
                        if (isExpanded) {
                            stringResource(R.string.a11y_action_label_collapse)
                        } else {
                            stringResource(R.string.a11y_action_label_expand)
                        },
                    onClick = { onToggleCollectionExpanded(collection, !isExpanded) },
                )
                .height(48.dp),
        shape = if (isExpanded) expandedCollectionShape else collapsedCollectionShape,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceBright),
        elevation = CardDefaults.cardElevation(defaultElevation = 5.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxHeight(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_collection_24),
                contentDescription = null,
                modifier =
                    Modifier.padding(
                        start = 16.dp,
                        end = 8.dp, // (24.dp - 16.dp) hardcoded in ExpandableListHeader
                    ),
                tint = collection.iconColor(),
            )

            ExpandableListHeader(
                headerText = collection.title,
                headerTextStyle = FirefoxTheme.typography.headline7,
                expanded = isExpanded,
            ) {
                if (isExpanded) {
                    Row {
                        IconButton(
                            onClick = { onCollectionShareTabsClicked(collection) },
                            contentDescription = stringResource(string.share_button_content_description),
                        ) {
                            Icon(
                                painter = painterResource(iconsR.drawable.mozac_ic_share_android_24),
                                contentDescription = null,
                            )
                        }

                        IconButton(
                            onClick = {
                                isMenuExpanded = !isMenuExpanded
                            },
                            contentDescription = stringResource(string.collection_menu_button_content_description),
                        ) {
                            Icon(
                                painter = painterResource(iconsR.drawable.mozac_ic_more_vertical_round_24),
                                contentDescription = null,
                            )

                            ContextualMenu(
                                showMenu = isMenuExpanded,
                                menuItems = menuItems,
                                onDismissRequest = { isMenuExpanded = false },
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun TabCollection.iconColor(): Color {
    val palette = FirefoxTheme.tabGroupColors
    val colors = listOf(palette.purple, palette.blue, palette.pink, palette.green, palette.yellow)
    return colors[abs(id % colors.size).toInt()].primary
}

@Composable
@PreviewLightDark
private fun CollectionExpandedPreview() {
    FirefoxTheme {
        var expanded by remember { mutableStateOf(true) }

        Collection(
            collection = FakeHomepagePreview.collection(),
            expanded = expanded,
            menuItems = emptyList(),
            onToggleCollectionExpanded = { _, expand ->
                expanded = expand
            },
            onCollectionShareTabsClicked = {},
        )
    }
}

@Composable
@PreviewLightDark
private fun CollectionPreview() {
    FirefoxTheme {
        var expanded by remember { mutableStateOf(false) }

        Collection(
            collection = FakeHomepagePreview.collection(),
            expanded = expanded,
            menuItems = emptyList(),
            onToggleCollectionExpanded = { _, expand ->
                expanded = expand
            },
            onCollectionShareTabsClicked = {},
        )
    }
}
