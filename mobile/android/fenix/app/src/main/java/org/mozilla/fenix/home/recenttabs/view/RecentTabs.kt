/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.recenttabs.view

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CardDefaults
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
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.browser.icons.compose.Loader
import mozilla.components.browser.icons.compose.Placeholder
import mozilla.components.browser.icons.compose.WithIcon
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.utils.inComposePreview
import mozilla.components.support.ktx.kotlin.trimmed
import mozilla.components.support.ktx.util.URLStringUtils
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.Image
import org.mozilla.fenix.compose.TabThumbnail
import org.mozilla.fenix.compose.thumbnailImageData
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.topsites.ui.HomepageCard
import org.mozilla.fenix.home.topsites.ui.homepageCardImageShape
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.wallpapers.WallpaperTheme

private const val THUMBNAIL_SIZE = 108

/**
 * A list of recent tabs to jump back to.
 *
 * @param recentTabs List of [RecentTab] to display.
 * @param menuItems List of [RecentTabMenuItem] shown long clicking a [RecentTab].
 * @param backgroundColor The background [Color] of each item.
 * @param onRecentTabClick Invoked when the user clicks on a recent tab.
 */
@Composable
fun RecentTabs(
    recentTabs: List<RecentTab>,
    menuItems: List<RecentTabMenuItem>,
    backgroundColor: Color = WallpaperTheme.cardBackgroundColor,
    onRecentTabClick: (String) -> Unit = {},
) {
    Column(
        modifier =
            Modifier.fillMaxWidth().semantics {
                testTagsAsResourceId = true
                testTag = "recent.tabs"
            },
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        recentTabs.forEach { tab ->
            when (tab) {
                is RecentTab.Tab -> {
                    RecentTabItem(
                        tab = tab,
                        menuItems = menuItems,
                        backgroundColor = backgroundColor,
                        onRecentTabClick = onRecentTabClick,
                    )
                }
            }
        }
    }
}

/**
 * A recent tab item.
 *
 * @param tab [RecentTab.Tab] that was recently viewed.
 * @param menuItems List of [RecentTabMenuItem] to be shown in a menu.
 * @param backgroundColor The background [Color] of the item.
 * @param onRecentTabClick Invoked when the user clicks on a recent tab.
 */
@Composable
@Suppress("LongMethod")
private fun RecentTabItem(
    tab: RecentTab.Tab,
    menuItems: List<RecentTabMenuItem>,
    backgroundColor: Color,
    onRecentTabClick: (String) -> Unit = {},
) {
    var isMenuExpanded by remember { mutableStateOf(false) }

    HomepageCard(
        modifier =
            Modifier.fillMaxWidth()
                .wrapContentHeight()
                .combinedClickable(
                    enabled = true,
                    onClick = { onRecentTabClick(tab.state.id) },
                    onLongClick = { isMenuExpanded = true },
                ),
        backgroundColor = backgroundColor,
        shape = MaterialTheme.shapes.small,
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
        border = null,
    ) {
        Row(
            modifier =
                Modifier.padding(
                    start = FirefoxTheme.layout.space.static50,
                    top = FirefoxTheme.layout.space.static50,
                    bottom = FirefoxTheme.layout.space.static50,
                    end = FirefoxTheme.layout.space.static100,
                ),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            RecentTabImage(
                tab = tab,
                modifier = Modifier.size(108.dp, 80.dp).clip(homepageCardImageShape),
            )

            Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))

            val displayUrl = URLStringUtils.toLosslessDisplayUrl(tab.state.content.url).trimmed()

            Column(
                modifier = Modifier.fillMaxWidth().wrapContentHeight(),
                verticalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(
                    text = tab.state.content.title.ifEmpty { displayUrl },
                    modifier =
                        Modifier.semantics {
                            testTagsAsResourceId = true
                            testTag = "recent.tab.title"
                        },
                    color = MaterialTheme.colorScheme.onSurface,
                    fontSize = 14.sp,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )

                Row(verticalAlignment = Alignment.CenterVertically) {
                    RecentTabIcon(
                        url = tab.state.content.url,
                        modifier = Modifier.size(18.dp).clip(RoundedCornerShape(2.dp)),
                        contentScale = ContentScale.Crop,
                        icon = tab.state.content.icon,
                    )

                    Spacer(modifier = Modifier.width(8.dp))

                    Text(
                        text = displayUrl,
                        modifier =
                            Modifier.semantics {
                                testTagsAsResourceId = true
                                testTag = "recent.tab.url"
                            },
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 12.sp,
                        overflow = TextOverflow.Ellipsis,
                        maxLines = 1,
                    )
                }
            }

            DropdownMenu(
                menuItems =
                    menuItems.map { item ->
                        MenuItem.TextItem(Text.String(item.title)) { item.onClick(tab) }
                    },
                expanded = isMenuExpanded,
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = "recent.tab.menu"
                    },
                onDismissRequest = { isMenuExpanded = false },
            )
        }
    }
}

/**
 * A recent tab image.
 *
 * @param tab [RecentTab] that was recently viewed.
 * @param modifier [Modifier] used to draw the image content.
 */
@Composable
fun RecentTabImage(
    tab: RecentTab.Tab,
    modifier: Modifier = Modifier,
) {
    val previewImageUrl = tab.state.content.previewImageUrl

    when {
        !previewImageUrl.isNullOrEmpty() -> {
            Image(
                url = previewImageUrl,
                modifier = modifier,
                targetSize = THUMBNAIL_SIZE.dp,
                contentScale = ContentScale.Crop,
                fallback = {
                    TabThumbnail(
                        tabThumbnailImageData = tab.state.thumbnailImageData(),
                        thumbnailSizePx = LocalDensity.current.run { THUMBNAIL_SIZE.dp.toPx().toInt() },
                        modifier = modifier,
                    )
                },
            )
        }
        else ->
            TabThumbnail(
                tabThumbnailImageData = tab.state.thumbnailImageData(),
                thumbnailSizePx = LocalDensity.current.run { THUMBNAIL_SIZE.dp.toPx().toInt() },
                modifier = modifier,
            )
    }
}

/**
 * A recent tab icon.
 *
 * @param url The loaded URL of the tab.
 * @param modifier [Modifier] used to draw the image content.
 * @param contentScale [ContentScale] used to draw image content.
 * @param alignment [Alignment] used to draw the image content.
 * @param icon The icon of the tab. Fallback to loading the icon from the [url] if the [icon] is null.
 */
@Composable
private fun RecentTabIcon(
    url: String,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Fit,
    alignment: Alignment = Alignment.Center,
    icon: Bitmap? = null,
) {
    when {
        icon != null -> {
            Image(
                painter = BitmapPainter(icon.asImageBitmap()),
                contentDescription = null,
                modifier = modifier,
                contentScale = contentScale,
                alignment = alignment,
            )
        }
        !inComposePreview -> {
            components.core.icons.Loader(url) {
                Placeholder {
                    PlaceHolderTabIcon(modifier)
                }

                WithIcon { icon ->
                    Image(
                        painter = icon.painter,
                        contentDescription = null,
                        modifier = modifier,
                        contentScale = contentScale,
                    )
                }
            }
        }
        else -> {
            PlaceHolderTabIcon(modifier)
        }
    }
}

/**
 * A placeholder for the recent tab icon.
 *
 * @param modifier [Modifier] used to shape the content.
 */
@Composable
private fun PlaceHolderTabIcon(modifier: Modifier) {
    Box(modifier = modifier.background(color = MaterialTheme.colorScheme.surfaceContainerHighest))
}

@PreviewLightDark
@Composable
private fun RecentTabsPreview() {
    FirefoxTheme {
        Surface {
            Column(modifier = Modifier.padding(all = FirefoxTheme.layout.space.static200)) {
                RecentTabs(
                    recentTabs = FakeHomepagePreview.recentTabs(),
                    menuItems =
                        listOf(
                            RecentTabMenuItem(
                                title = "Menu item",
                                onClick = {},
                            )
                        ),
                    onRecentTabClick = {},
                )
            }
        }
    }
}
