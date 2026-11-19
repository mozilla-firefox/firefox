/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.feature.tab.collections.Tab
import mozilla.components.feature.tab.collections.TabCollection
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.MenuItem
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.sessioncontrol.CollectionInteractor
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * List of expandable collections.
 *
 * @param modifier for the UI.
 * @param collections List of [TabCollection] to display.
 * @param expandedCollections List of ids corresponding to [TabCollection]s which are currently expanded.
 * @param interactor Interactor for interactions with the UI.
 */
@Composable
fun Collections(
    modifier: Modifier = Modifier,
    collections: List<TabCollection>,
    expandedCollections: Set<Long> = emptySet(),
    interactor: CollectionInteractor,
) {
    Column(modifier = modifier) {
        for (collection in collections) {
            Spacer(Modifier.height(12.dp))

            Collection(
                collection = collection,
                expanded = expandedCollections.contains(collection.id),
                menuItems =
                    getMenuItems(
                        collection = collection,
                        onOpenTabsTapped = interactor::onCollectionOpenTabsTapped,
                        onDeleteCollectionTapped = interactor::onDeleteCollectionTapped,
                    ),
                onToggleCollectionExpanded = interactor::onToggleCollectionExpanded,
                onCollectionShareTabsClicked = interactor::onCollectionShareTabsClicked,
            )

            if (expandedCollections.contains(collection.id)) {
                val lastId = collection.tabs.last().id

                for (tab in collection.tabs) {
                    key(tab.id) {
                        CollectionItem(
                            tab = tab,
                            isLastInCollection = tab.id == lastId,
                            onClick = { interactor.onCollectionOpenTabClicked(tab) },
                            onRemove = {
                                interactor.onCollectionRemoveTab(
                                    collection = collection,
                                    tab = tab,
                                )
                            },
                        )
                    }
                }
            }
        }
    }
}

/**
 * Constructs and returns the default list of menu options for a [TabCollection].
 *
 * @param collection [TabCollection] for which the menu will be shown. Might serve as an argument for the callbacks for
 *   when the user interacts with certain menu options.
 * @param onOpenTabsTapped Invoked when the user chooses to open the tabs from [collection].
 * @param onDeleteCollectionTapped Invoked when the user chooses to delete [collection].
 */
@Composable
private fun getMenuItems(
    collection: TabCollection,
    onOpenTabsTapped: (TabCollection) -> Unit,
    onDeleteCollectionTapped: (TabCollection) -> Unit,
): List<MenuItem> {
    return listOf(
        MenuItem(
            title = stringResource(R.string.collection_open_tabs),
            color = MaterialTheme.colorScheme.onSurface,
        ) {
            onOpenTabsTapped(collection)
        },
        MenuItem(
            title = stringResource(R.string.collection_delete),
            color = MaterialTheme.colorScheme.error,
        ) {
            onDeleteCollectionTapped(collection)
        },
    )
}

@PreviewLightDark
@Composable
private fun CollectionsPreview() {
    val expandedCollections: MutableState<Set<Long>> = remember { mutableStateOf(setOf(1L)) }

    FirefoxTheme {
        Surface {
            Collections(
                modifier = Modifier.padding(8.dp),
                collections =
                    listOf(
                        FakeHomepagePreview.collection(
                            tabs =
                                listOf(
                                    FakeHomepagePreview.tab(),
                                    FakeHomepagePreview.tab(),
                                    FakeHomepagePreview.tab(),
                                )
                        ),
                        FakeHomepagePreview.collection(
                            tabs =
                                listOf(
                                    FakeHomepagePreview.tab(),
                                    FakeHomepagePreview.tab(),
                                )
                        ),
                    ),
                expandedCollections = expandedCollections.value,
                interactor =
                    object : CollectionInteractor {
                        override fun onCollectionOpenTabClicked(tab: Tab) {
                            /* no op */
                        }

                        override fun onCollectionOpenTabsTapped(collection: TabCollection) {
                            /* no op */
                        }

                        override fun onCollectionRemoveTab(
                            collection: TabCollection,
                            tab: Tab,
                        ) {
                            /* no op */
                        }

                        override fun onCollectionShareTabsClicked(collection: TabCollection) {
                            /* no op */
                        }

                        override fun onDeleteCollectionTapped(collection: TabCollection) {
                            /* no op */
                        }

                        override fun onToggleCollectionExpanded(
                            collection: TabCollection,
                            expand: Boolean,
                        ) {
                            expandedCollections.value =
                                if (expand) {
                                    setOf(1L)
                                } else {
                                    setOf()
                                }
                        }
                    },
            )
        }
    }
}
