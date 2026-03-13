/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.AbsoluteAlignment
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.modifier.rightClickable
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.ui.colors.PhotonColors
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.ContextualMenu
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.MenuItem
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.topsites.TopSitesTestTag.TOP_SITE_CARD_FAVICON
import org.mozilla.fenix.home.topsites.interactor.TopSiteInteractor
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.wallpapers.WallpaperState
import mozilla.components.ui.icons.R as iconsR

/**
 * The size of a top site item.
 */
const val TOP_SITES_ITEM_SIZE = 84

private const val TOP_SITES_TO_SHOW = 8
private const val TOP_SITES_PER_ROW = 4
private const val TOP_SITES_ROW_WIDTH = TOP_SITES_PER_ROW * TOP_SITES_ITEM_SIZE
private const val TOP_SITES_FAVICON_CARD_SIZE = 60
private const val TOP_SITES_FAVICON_SIZE = 36

/**
 * A list of top sites.
 *
 * @param topSites List of [TopSite] to display.
 * @param topSiteColors The color set defined by [TopSiteColors] used to style a top site.
 * @param interactor The interactor which handles user actions with the widget.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 */
@Composable
fun TopSites(
    topSites: List<TopSite>,
    topSiteColors: TopSiteColors = TopSiteColors.colors(),
    interactor: TopSiteInteractor,
    onTopSitesItemBound: () -> Unit,
) {
    TopSites(
        topSites = topSites,
        topSiteColors = topSiteColors,
        onTopSiteClick = { topSite ->
            interactor.onSelectTopSite(
                topSite = topSite,
                position = topSites.indexOf(topSite),
            )
        },
        onTopSiteLongClick = interactor::onTopSiteLongClicked,
        onTopSiteImpression = interactor::onTopSiteImpression,
        onOpenInPrivateTabClicked = interactor::onOpenInPrivateTabClicked,
        onEditTopSiteClicked = interactor::onEditTopSiteClicked,
        onRemoveTopSiteClicked = interactor::onRemoveTopSiteClicked,
        onSettingsClicked = interactor::onSettingsClicked,
        onSponsorPrivacyClicked = interactor::onSponsorPrivacyClicked,
        onTopSitesItemBound = onTopSitesItemBound,
    )
}

/**
 * A list of top sites.
 *
 * @param topSites List of [TopSite] to display.
 * @param topSiteColors The color set defined by [TopSiteColors] used to style a top site.
 * @param onTopSiteClick Invoked when the user clicks on a top site.
 * @param onTopSiteLongClick Invoked when the user long clicks on a top site.
 * @param onTopSiteImpression Invoked when the user sees a provided top site.
 * @param onOpenInPrivateTabClicked Invoked when the user clicks on the "Open in private tab"
 * menu item.
 * @param onEditTopSiteClicked Invoked when the user clicks on the "Edit" menu item.
 * @param onRemoveTopSiteClicked Invoked when the user clicks on the "Remove" menu item.
 * @param onSettingsClicked Invoked when the user clicks on the "Settings" menu item.
 * @param onSponsorPrivacyClicked Invoked when the user clicks on the "Our sponsors & your privacy"
 * menu item.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 */
@Composable
@Suppress("LongParameterList")
fun TopSites(
    topSites: List<TopSite>,
    topSiteColors: TopSiteColors = TopSiteColors.colors(),
    onTopSiteClick: (TopSite) -> Unit,
    onTopSiteLongClick: (TopSite) -> Unit,
    onTopSiteImpression: (TopSite.Provided, Int) -> Unit,
    onOpenInPrivateTabClicked: (topSite: TopSite) -> Unit,
    onEditTopSiteClicked: (topSite: TopSite) -> Unit,
    onRemoveTopSiteClicked: (topSite: TopSite) -> Unit,
    onSettingsClicked: () -> Unit,
    onSponsorPrivacyClicked: () -> Unit,
    onTopSitesItemBound: () -> Unit,
) {
    val topSitesToShow = topSites.take(TOP_SITES_TO_SHOW).chunked(TOP_SITES_PER_ROW)

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .semantics {
                testTagsAsResourceId = true
            }
            .testTag(TopSitesTestTag.TOP_SITES),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            modifier = Modifier.fillMaxWidth(),
            contentAlignment = Alignment.Center,
        ) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                for (items in topSitesToShow) {
                    Row(modifier = Modifier.defaultMinSize(minWidth = TOP_SITES_ROW_WIDTH.dp)) {
                        items.forEachIndexed { position, topSite ->
                            TopSiteItem(
                                topSite = topSite,
                                menuItems = getMenuItems(
                                    topSite = topSite,
                                    onOpenInPrivateTabClicked = onOpenInPrivateTabClicked,
                                    onEditTopSiteClicked = onEditTopSiteClicked,
                                    onRemoveTopSiteClicked = onRemoveTopSiteClicked,
                                    onSettingsClicked = onSettingsClicked,
                                    onSponsorPrivacyClicked = onSponsorPrivacyClicked,
                                ),
                                position = position,
                                topSiteColors = topSiteColors,
                                onTopSiteClick = { item -> onTopSiteClick(item) },
                                onTopSiteLongClick = onTopSiteLongClick,
                                onTopSiteImpression = onTopSiteImpression,
                                onTopSitesItemBound = onTopSitesItemBound,
                            )
                        }
                    }

                    if (items != topSitesToShow.last()) {
                        Spacer(modifier = Modifier.height(12.dp))
                    }
                }
            }
        }
    }
}

/**
 * Represents the colors used by top sites.
 */
data class TopSiteColors(
    val titleTextColor: Color,
    val sponsoredTextColor: Color,
    val faviconCardBackgroundColor: Color,
) {
    companion object {
        /**
         * Builder function used to construct an instance of [TopSiteColors].
         */
        @Composable
        fun colors(
            titleTextColor: Color = AcornTheme.colors.textNormal,
            sponsoredTextColor: Color = AcornTheme.colors.textSecondary,
            faviconCardBackgroundColor: Color = MaterialTheme.colorScheme.surfaceContainerLowest,
        ) = TopSiteColors(
            titleTextColor = titleTextColor,
            sponsoredTextColor = sponsoredTextColor,
            faviconCardBackgroundColor = faviconCardBackgroundColor,
        )

        /**
         * Builder function used to construct an instance of [TopSiteColors] given a
         * [WallpaperState].
         */
        @Composable
        fun colors(wallpaperState: WallpaperState): TopSiteColors {
            val textColor: Long? = wallpaperState.currentWallpaper.textColor
            val (titleTextColor, sponsoredTextColor) = if (textColor == null) {
                MaterialTheme.colorScheme.onSurface to MaterialTheme.colorScheme.onSurface
            } else {
                Color(textColor) to Color(textColor)
            }

            return TopSiteColors(
                titleTextColor = titleTextColor,
                sponsoredTextColor = sponsoredTextColor,
                faviconCardBackgroundColor = MaterialTheme.colorScheme.surfaceContainerLowest,
            )
        }
    }
}

/**
 * A top site item.
 *
 * @param topSite The [TopSite] to display.
 * @param menuItems List of [MenuItem]s to display in a top site dropdown menu.
 * @param position The position of the top site.
 * @param topSiteColors The color set defined by [TopSiteColors] used to style a top site.
 * @param onTopSiteClick Invoked when the user clicks on a top site.
 * @param onTopSiteLongClick Invoked when the user long clicks on a top site.
 * @param onTopSiteImpression Invoked when the user sees a provided top site.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 */
@Suppress(
    "LongMethod",
    "LongParameterList",
    "Deprecation",
) // https://bugzilla.mozilla.org/show_bug.cgi?id=1927713
@Composable
fun TopSiteItem(
    topSite: TopSite,
    menuItems: List<MenuItem>,
    position: Int,
    topSiteColors: TopSiteColors,
    onTopSiteClick: (TopSite) -> Unit,
    onTopSiteLongClick: (TopSite) -> Unit,
    onTopSiteImpression: (TopSite.Provided, Int) -> Unit,
    onTopSitesItemBound: () -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }
    val onLongClick = {
        onTopSiteLongClick(topSite)
        menuExpanded = true
    }

    Box(
        modifier = Modifier
            .semantics {
                testTagsAsResourceId = true
            }
            .testTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT),
    ) {
        Column(
            modifier = Modifier
                .combinedClickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                    onClick = { onTopSiteClick(topSite) },
                    onLongClick = onLongClick,
                )
                .rightClickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                    onRightClick = onLongClick,
                )
                .width(TOP_SITES_ITEM_SIZE.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(modifier = Modifier.height(4.dp))

            Box(
                modifier = Modifier.wrapContentSize(),
                contentAlignment = AbsoluteAlignment.TopLeft,
            ) {
                TopSiteFaviconCard(
                    topSite = topSite,
                    backgroundColor = topSiteColors.faviconCardBackgroundColor,
                )

                if (topSite is TopSite.Pinned || topSite is TopSite.Default) {
                    Box(
                        modifier = Modifier
                            .size(16.dp)
                            .background(color = PhotonColors.LightGrey20, shape = CircleShape),
                        contentAlignment = Alignment.Center,
                    ) {
                        Image(
                            modifier = Modifier.size(12.dp),
                            painter = painterResource(id = iconsR.drawable.mozac_ic_pin_fill_24),
                            colorFilter = ColorFilter.tint(PhotonColors.DarkGrey90),
                            contentDescription = null,
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(6.dp))

            Row(
                modifier = Modifier.width(TOP_SITES_ITEM_SIZE.dp),
                horizontalArrangement = Arrangement.Absolute.Center,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    modifier = Modifier
                        .semantics {
                            testTagsAsResourceId = true
                        }
                        .testTag(TopSitesTestTag.TOP_SITE_TITLE),
                    text = topSite.title ?: topSite.url,
                    color = topSiteColors.titleTextColor,
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 1,
                    style = FirefoxTheme.typography.caption.copy(fontWeight = FontWeight.W700),
                )
            }

            Text(
                text = if (topSite is TopSite.Provided) stringResource(id = R.string.top_sites_sponsored_label) else "",
                modifier = Modifier.width(TOP_SITES_ITEM_SIZE.dp),
                color = topSiteColors.sponsoredTextColor,
                fontSize = 10.sp,
                textAlign = TextAlign.Center,
                overflow = TextOverflow.Ellipsis,
                maxLines = 1,
                style = FirefoxTheme.typography.caption,
            )
        }

        ContextualMenu(
            modifier = Modifier
                .testTag(TopSitesTestTag.TOP_SITE_CONTEXTUAL_MENU),
            menuItems = menuItems,
            showMenu = menuExpanded,
            onDismissRequest = { menuExpanded = false },
        )

        if (topSite is TopSite.Provided) {
            LaunchedEffect(topSite) {
                onTopSiteImpression(topSite, position)
            }
        }
    }

    LaunchedEffect(Unit) {
        onTopSitesItemBound()
    }
}

/**
 * The top site favicon card.
 *
 * @param topSite The [TopSite] to display.
 * @param backgroundColor The background [Color] of the card.
 */
@Composable
private fun TopSiteFaviconCard(
    topSite: TopSite,
    backgroundColor: Color,
) {
    Card(
        modifier = Modifier
            .semantics {
                testTagsAsResourceId = true
                testTag = TOP_SITE_CARD_FAVICON
            }
            .size(TOP_SITES_FAVICON_CARD_SIZE.dp),
        shape = CircleShape,
        colors = CardDefaults.cardColors(containerColor = backgroundColor),
        elevation = CardDefaults.cardElevation(defaultElevation = 6.dp),
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center,
        ) {
            Surface(
                modifier = Modifier.size(TOP_SITES_FAVICON_SIZE.dp),
                color = backgroundColor,
                shape = RoundedCornerShape(4.dp),
            ) {
                TopSiteFavicon(topSite = topSite)
            }
        }
    }
}

@Composable
private fun TopSiteFavicon(topSite: TopSite) {
    when (val favicon = getTopSitesFavicon(topSite)) {
        is TopSitesFavicon.ImageUrl -> Favicon(
            url = topSite.url,
            size = TOP_SITES_FAVICON_SIZE.dp,
            imageUrl = favicon.imageUrl,
        )

        is TopSitesFavicon.Drawable -> Favicon(
            size = TOP_SITES_FAVICON_SIZE.dp,
            imageResource = favicon.drawableResId,
        )
    }
}

@Composable
internal fun getMenuItems(
    topSite: TopSite,
    onOpenInPrivateTabClicked: (topSite: TopSite) -> Unit,
    onEditTopSiteClicked: (topSite: TopSite) -> Unit,
    onRemoveTopSiteClicked: (topSite: TopSite) -> Unit,
    onSettingsClicked: () -> Unit,
    onSponsorPrivacyClicked: () -> Unit,
): List<MenuItem> {
    val isPinnedSite = topSite is TopSite.Pinned || topSite is TopSite.Default
    val isProvidedSite = topSite is TopSite.Provided
    val isFrecentSite = topSite is TopSite.Frecent
    val result = mutableListOf<MenuItem>()

    result.add(
        MenuItem(
            title = stringResource(id = R.string.bookmark_menu_open_in_private_tab_button),
            testTag = TopSitesTestTag.OPEN_IN_PRIVATE_TAB,
            onClick = { onOpenInPrivateTabClicked(topSite) },
        ),
    )

    if (isPinnedSite || isFrecentSite) {
        result.add(
            MenuItem(
                title = stringResource(id = R.string.top_sites_edit_top_site),
                testTag = TopSitesTestTag.EDIT,
                onClick = { onEditTopSiteClicked(topSite) },
            ),
        )
    }

    if (!isProvidedSite) {
        result.add(
            MenuItem(
                title = stringResource(
                    id = if (isPinnedSite) {
                        R.string.remove_top_site
                    } else {
                        R.string.delete_from_history
                    },
                ),
                testTag = TopSitesTestTag.REMOVE,
                onClick = { onRemoveTopSiteClicked(topSite) },
            ),
        )
    }

    if (isProvidedSite) {
        result.addAll(
            listOf(
                MenuItem(
                    title = stringResource(id = R.string.top_sites_menu_settings),
                    onClick = onSettingsClicked,
                ),
                MenuItem(
                    title = stringResource(id = R.string.top_sites_menu_sponsor_privacy),
                    onClick = onSponsorPrivacyClicked,
                ),
            ),
        )
    }

    return result
}

@FlexibleWindowPreview
@Composable
private fun TopSitesPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Surface {
            Box(
                modifier = Modifier.padding(all = FirefoxTheme.layout.space.static200),
            ) {
                TopSites(
                    topSites = FakeHomepagePreview.topSites(),
                    onTopSiteClick = {},
                    onTopSiteLongClick = {},
                    onTopSiteImpression = { _, _ -> },
                    onOpenInPrivateTabClicked = {},
                    onEditTopSiteClicked = {},
                    onRemoveTopSiteClicked = {},
                    onSettingsClicked = {},
                    onSponsorPrivacyClicked = {},
                    onTopSitesItemBound = {},
                )
            }
        }
    }
}
