/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.bookmarks.view

import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.browser.icons.compose.Loader
import mozilla.components.browser.icons.compose.Placeholder
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.utils.inComposePreview
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.ContextualMenu
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.Image
import org.mozilla.fenix.compose.MenuItem
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.fake.FakeHomepagePreview.bookmarks
import org.mozilla.fenix.home.topsites.ui.HomepageCard
import org.mozilla.fenix.home.topsites.ui.homepageCardImageShape
import org.mozilla.fenix.theme.FirefoxTheme

private val imageWidth = 420.dp
private val imageHeight = 190.dp

private val imageModifier = Modifier
    .size(width = imageWidth, height = imageHeight)
    .clip(homepageCardImageShape)

/**
 * A list of bookmarks.
 *
 * @param bookmarks List of [Bookmark]s to display.
 * @param menuItems List of [BookmarksMenuItem] shown when long clicking a [BookmarkItem]
 * @param backgroundColor The background [Color] of each bookmark.
 * @param onBookmarkClick Invoked when the user clicks on a bookmark.
 */
@Composable
fun Bookmarks(
    bookmarks: List<Bookmark>,
    menuItems: List<BookmarksMenuItem>,
    backgroundColor: Color,
    onBookmarkClick: (Bookmark) -> Unit = {},
) {
    LazyRow(
        modifier = Modifier
            .fillMaxWidth()
            .semantics {
                testTagsAsResourceId = true
                testTag = "bookmarks"
            },
        contentPadding = PaddingValues(horizontal = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        items(bookmarks) { bookmark ->
            BookmarkItem(
                bookmark = bookmark,
                menuItems = menuItems,
                backgroundColor = backgroundColor,
                onBookmarkClick = onBookmarkClick,
            )
        }
    }
}

/**
 * A bookmark item.
 *
 * @param bookmark The [Bookmark] to display.
 * @param menuItems The list of [BookmarksMenuItem] shown when long clicking on the bookmark item.
 * @param backgroundColor The background [Color] of the bookmark item.
 * @param onBookmarkClick Invoked when the user clicks on the bookmark item.
 */
@Composable
@Suppress("Deprecation") // https://bugzilla.mozilla.org/show_bug.cgi?id=1927718
private fun BookmarkItem(
    bookmark: Bookmark,
    menuItems: List<BookmarksMenuItem>,
    backgroundColor: Color,
    onBookmarkClick: (Bookmark) -> Unit = {},
) {
    var isMenuExpanded by remember { mutableStateOf(false) }

    HomepageCard(
        modifier = Modifier
            .width(420.dp)
            .height(252.dp)
            .combinedClickable(
                enabled = true,
                onClick = { onBookmarkClick(bookmark) },
                onLongClick = { isMenuExpanded = true },
            ),
        backgroundColor = backgroundColor,
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(
                    top = FirefoxTheme.layout.space.static100,
                    bottom = FirefoxTheme.layout.space.static100,
                    start = FirefoxTheme.layout.space.static50,
                    end = FirefoxTheme.layout.space.static50,
                ),
        ) {
            BookmarkImage(bookmark)

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = bookmark.title ?: bookmark.url ?: "",
                modifier = Modifier
                    .padding(horizontal = 16.dp)
                    .semantics {
                        testTagsAsResourceId = true
                        testTag = "bookmark.title"
                    },
                overflow = TextOverflow.Ellipsis,
                maxLines = 1,
                color = AcornTheme.colors.textNormal,
                style = FirefoxTheme.typography.caption,
            )

            ContextualMenu(
                showMenu = isMenuExpanded,
                onDismissRequest = { isMenuExpanded = false },
                menuItems = menuItems.map { item -> MenuItem(item.title) { item.onClick(bookmark) } },
                modifier = Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = "bookmark.menu"
                },
            )
        }
    }
}

@Composable
private fun BookmarkImage(bookmark: Bookmark) {
    when {
        !bookmark.previewImageUrl.isNullOrEmpty() -> {
            Image(
                url = bookmark.previewImageUrl,
                modifier = imageModifier,
                targetSize = imageWidth,
                contentScale = ContentScale.Crop,
                fallback = {
                    if (!bookmark.url.isNullOrEmpty()) {
                        FallbackBookmarkFaviconImage(url = bookmark.url)
                    }
                },
            )
        }
        !bookmark.url.isNullOrEmpty() && !inComposePreview -> {
            components.core.icons.Loader(bookmark.url) {
                Placeholder {
                    PlaceholderBookmarkImage()
                }

                FallbackBookmarkFaviconImage(bookmark.url)
            }
        }
        inComposePreview -> {
            PlaceholderBookmarkImage()
        }
    }
}

@Composable
private fun PlaceholderBookmarkImage() {
    Box(
        modifier = imageModifier.background(
            color = AcornTheme.colors.backgroundPlate,
        ),
    )
}

@Composable
private fun FallbackBookmarkFaviconImage(
    url: String,
) {
    Box(
        modifier = imageModifier.background(
            color = MaterialTheme.colorScheme.surfaceContainerLowest,
        ),
        contentAlignment = Alignment.Center,
    ) {
        Favicon(url = url, size = 80.dp)
    }
}

@Composable
@PreviewLightDark
private fun BookmarksPreview() {
    FirefoxTheme {
        Surface {
            Bookmarks(
                bookmarks = bookmarks(),
                menuItems = listOf(),
                backgroundColor = AcornTheme.colors.backgroundPlate,
            )
        }
    }
}
