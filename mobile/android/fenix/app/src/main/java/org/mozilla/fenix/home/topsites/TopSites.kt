/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.AbsoluteAlignment
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.modifier.rightClickable
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.ContextualMenu
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.MenuItem
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.topsites.TopSitesTestTag.TOP_SITE_CARD_FAVICON
import org.mozilla.fenix.home.topsites.interactor.TopSiteInteractor
import org.mozilla.fenix.home.topsites.ui.Shortcuts
import org.mozilla.fenix.home.ui.horizontalMargin
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.wallpapers.WallpaperState

/** The size of a top site item. */
const val TOP_SITES_ITEM_SIZE = 84

internal const val TOP_SITES_PER_ROW = 4
internal const val TOP_SITES_FAVICON_CARD_SIZE = 60
internal const val TOP_SITES_FAVICON_SIZE = 36

/**
 * A list of top sites.
 *
 * @param state [TopSiteState] describing the top sites to display.
 * @param interactor The interactor which handles user actions with the widget.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 * @param onAddShortcutClicked Invoked when the user clicks on the "Add shortcut" tile.
 */
@Composable
internal fun TopSites(
    state: TopSiteState,
    interactor: TopSiteInteractor,
    onTopSitesItemBound: () -> Unit,
    onAddShortcutClicked: () -> Unit,
) {
    // Deliberately not persisted: every new homepage starts collapsed.
    var isExpanded by remember { mutableStateOf(false) }

    TopSites(
        topSites = state.topSites,
        topSiteColors = state.colors,
        onTopSiteClick = { topSite ->
            interactor.onSelectTopSite(
                topSite = topSite,
                position = state.topSites.indexOf(topSite),
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
        onAddShortcutClicked = onAddShortcutClicked,
        onExpandToggleClick = {
            val expanded = !isExpanded
            isExpanded = expanded
            interactor.onExpandToggleClicked(expanded)
        },
        isExpandToggleEnabled = state.isExpandToggleEnabled,
        isExpanded = isExpanded,
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
 * @param onOpenInPrivateTabClicked Invoked when the user clicks on the "Open in private tab" menu item.
 * @param onEditTopSiteClicked Invoked when the user clicks on the "Edit" menu item.
 * @param onRemoveTopSiteClicked Invoked when the user clicks on the "Remove" menu item.
 * @param onSettingsClicked Invoked when the user clicks on the "Settings" menu item.
 * @param onSponsorPrivacyClicked Invoked when the user clicks on the "Our sponsors & your privacy" menu item.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 * @param onAddShortcutClicked Invoked when the user clicks on the "Add shortcut" tile.
 * @param onExpandToggleClick Invoked when the user clicks on the expand/collapse control.
 * @param isExpandToggleEnabled Whether the control that expands and collapses the grid is enabled.
 * @param isExpanded Whether every top site is shown rather than only the first [TOP_SITES_COLLAPSED_ROWS] rows.
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
    onAddShortcutClicked: () -> Unit,
    onExpandToggleClick: () -> Unit = {},
    isExpandToggleEnabled: Boolean = false,
    isExpanded: Boolean = false,
) {
    BoxWithConstraints(
        modifier =
            Modifier.fillMaxWidth()
                .semantics {
                    testTagsAsResourceId = true
                }
                .testTag(TopSitesTestTag.TOP_SITES)
                .padding(horizontal = horizontalMargin)
    ) {
        val columns = remember(maxWidth) { calculateTopSitesRowLayout(maxWidth).columns }
        val collapsedCount = collapsedTopSitesCount(columns)

        val hasHiddenShortcuts = topSites.size > collapsedCount
        val hasHiddenAddShortcut = topSites.size >= collapsedCount

        Column(
            modifier = Modifier.fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Shortcuts(
                topSites = if (isExpanded) topSites else topSites.take(collapsedCount),
                topSiteColors = topSiteColors,
                showAddShortcut = isExpanded || topSites.size < collapsedCount,
                scrollable = false,
                menuItems = { topSite ->
                    getMenuItems(
                        topSite = topSite,
                        onOpenInPrivateTabClicked = onOpenInPrivateTabClicked,
                        onEditTopSiteClicked = onEditTopSiteClicked,
                        onRemoveTopSiteClicked = onRemoveTopSiteClicked,
                        onSettingsClicked = onSettingsClicked,
                        onSponsorPrivacyClicked = onSponsorPrivacyClicked,
                    )
                },
                onTopSiteClick = onTopSiteClick,
                onTopSiteLongClick = onTopSiteLongClick,
                onTopSiteImpression = onTopSiteImpression,
                onTopSitesItemBound = onTopSitesItemBound,
                onAddShortcutClicked = onAddShortcutClicked,
            )

            if (isExpandToggleEnabled && (hasHiddenShortcuts || hasHiddenAddShortcut)) {
                Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

                TopSitesExpandToggle(
                    isExpanded = isExpanded,
                    contentColor = topSiteColors.titleTextColor,
                    onClick = onExpandToggleClick,
                )
            }
        }
    }
}

@Composable
private fun TopSitesExpandToggle(
    isExpanded: Boolean,
    contentColor: Color,
    onClick: () -> Unit,
) {
    TextButton(
        onClick = onClick,
        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }.testTag(TopSitesTestTag.EXPAND_TOGGLE),
        colors = ButtonDefaults.textButtonColors(contentColor = contentColor),
    ) {
        Icon(
            painter =
                painterResource(
                    if (isExpanded) {
                        iconsR.drawable.mozac_ic_chevron_up_16
                    } else {
                        iconsR.drawable.mozac_ic_chevron_down_16
                    }
                ),
            contentDescription = null,
            tint = LocalContentColor.current,
        )

        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))

        Text(
            text =
                stringResource(
                    if (isExpanded) {
                        R.string.homepage_shortcuts_show_less
                    } else {
                        R.string.homepage_shortcuts_show_all
                    }
                ),
            style = FirefoxTheme.typography.button,
            maxLines = 1,
        )
    }
}

/** Represents the colors used by top sites. */
data class TopSiteColors(
    val titleTextColor: Color,
    val sponsoredTextColor: Color,
    val faviconCardBackgroundColor: Color,
) {
    companion object {
        /** Builder function used to construct an instance of [TopSiteColors]. */
        @Composable
        @ReadOnlyComposable
        fun colors(
            titleTextColor: Color = MaterialTheme.colorScheme.onSurface,
            sponsoredTextColor: Color = MaterialTheme.colorScheme.onSurface,
            faviconCardBackgroundColor: Color = MaterialTheme.colorScheme.surfaceBright,
        ) =
            TopSiteColors(
                titleTextColor = titleTextColor,
                sponsoredTextColor = sponsoredTextColor,
                faviconCardBackgroundColor = faviconCardBackgroundColor,
            )

        /** Builder function used to construct an instance of [TopSiteColors] given a [WallpaperState]. */
        @Composable
        @ReadOnlyComposable
        fun colors(wallpaperState: WallpaperState): TopSiteColors {
            val textColor: Long? = wallpaperState.currentWallpaper.textColor
            val (titleTextColor, sponsoredTextColor) =
                if (textColor == null) {
                    MaterialTheme.colorScheme.onSurface to MaterialTheme.colorScheme.onSurface
                } else {
                    Color(textColor) to Color(textColor)
                }

            return TopSiteColors(
                titleTextColor = titleTextColor,
                sponsoredTextColor = sponsoredTextColor,
                faviconCardBackgroundColor = MaterialTheme.colorScheme.surfaceBright,
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
 * @param itemLayout The sizes to lay the top site out with, adapted to the available width.
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
    itemLayout: TopSiteItemLayout = TopSiteItemLayout.sizes(),
) {
    var menuExpanded by remember { mutableStateOf(false) }
    val onLongClick = {
        onTopSiteLongClick(topSite)
        menuExpanded = true
    }

    Box(
        modifier =
            Modifier.semantics {
                    testTagsAsResourceId = true
                }
                .testTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT)
    ) {
        Column(
            modifier =
                Modifier.combinedClickable(
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
                    .width(itemLayout.itemWidth),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(modifier = Modifier.height(TOP_SITES_ITEM_TOP_SPACING))

            Box(
                modifier = Modifier.wrapContentSize(),
                contentAlignment = AbsoluteAlignment.TopLeft,
            ) {
                TopSiteFaviconCard(
                    topSite = topSite,
                    backgroundColor = topSiteColors.faviconCardBackgroundColor,
                    faviconCardSize = itemLayout.faviconCardSize,
                    faviconSize = itemLayout.faviconSize,
                )

                if (topSite is TopSite.Pinned || topSite is TopSite.DefaultPinned) {
                    Box(
                        modifier = Modifier.size(16.dp),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            painter = painterResource(id = iconsR.drawable.mozac_ic_pin_8),
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.secondary,
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(TOP_SITES_ITEM_BOTTOM_SPACING))

            TopSiteItemText(
                title = topSite.title ?: topSite.url,
                maxTitleLines = topSite.getMaxLinesForTitle(),
                isSponsored = topSite is TopSite.Provided,
                topSiteColors = topSiteColors,
                itemWidth = itemLayout.itemWidth,
            )
        }

        ContextualMenu(
            modifier = Modifier.testTag(TopSitesTestTag.TOP_SITE_CONTEXTUAL_MENU),
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
 * The title and sponsored label a shortcut lays out under its favicon.
 *
 * @param title The shortcut's title.
 * @param maxTitleLines Lines the title may wrap onto.
 * @param isSponsored Whether to label the shortcut as sponsored. The label is laid out either way so every shortcut is
 *   the same height.
 * @param topSiteColors The color set defined by [TopSiteColors] used to style a top site.
 * @param itemWidth Width of the shortcut cell, which bounds the text.
 */
@Composable
internal fun TopSiteItemText(
    title: String,
    maxTitleLines: Int,
    isSponsored: Boolean,
    topSiteColors: TopSiteColors,
    itemWidth: Dp,
) {
    Row(
        modifier = Modifier.width(itemWidth),
        horizontalArrangement = Arrangement.Absolute.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            modifier =
                Modifier.semantics {
                        testTagsAsResourceId = true
                    }
                    .padding(horizontal = 4.dp)
                    .testTag(TopSitesTestTag.TOP_SITE_TITLE),
            text = title,
            color = topSiteColors.titleTextColor,
            textAlign = TextAlign.Center,
            overflow = TextOverflow.Ellipsis,
            maxLines = maxTitleLines,
            style = FirefoxTheme.typography.caption.copy(fontWeight = FontWeight.W700),
        )
    }

    Text(
        text = if (isSponsored) stringResource(id = R.string.top_sites_sponsored_label) else "",
        modifier = Modifier.width(itemWidth),
        color = topSiteColors.sponsoredTextColor,
        fontSize = 10.sp,
        textAlign = TextAlign.Center,
        overflow = TextOverflow.Ellipsis,
        maxLines = 1,
        style = FirefoxTheme.typography.caption,
    )
}

/**
 * The top site favicon card.
 *
 * @param topSite The [TopSite] to display.
 * @param backgroundColor The background [Color] of the card.
 * @param faviconCardSize The size of the card.
 * @param faviconSize The size of the favicon image inside the card.
 */
@Composable
private fun TopSiteFaviconCard(
    topSite: TopSite,
    backgroundColor: Color,
    faviconCardSize: Dp = TOP_SITES_FAVICON_CARD_SIZE.dp,
    faviconSize: Dp = TOP_SITES_FAVICON_SIZE.dp,
) {
    Card(
        modifier =
            Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = TOP_SITE_CARD_FAVICON
                }
                .size(faviconCardSize),
        shape = CircleShape,
        colors = CardDefaults.cardColors(containerColor = backgroundColor),
        elevation = CardDefaults.cardElevation(defaultElevation = 6.dp),
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center,
        ) {
            Surface(
                modifier = Modifier.size(faviconSize),
                color = backgroundColor,
                shape = MaterialTheme.shapes.extraSmall,
            ) {
                TopSiteFavicon(topSite = topSite, faviconSize = faviconSize)
            }
        }
    }
}

@Composable
private fun TopSiteFavicon(
    topSite: TopSite,
    faviconSize: Dp = TOP_SITES_FAVICON_SIZE.dp,
) {
    when (val favicon = getTopSitesFavicon(topSite)) {
        is TopSitesFavicon.ImageUrl ->
            Favicon(
                url = topSite.url,
                size = faviconSize,
                imageUrl = favicon.imageUrl,
            )

        is TopSitesFavicon.Drawable ->
            Favicon(
                size = faviconSize,
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
    val isPinnedOrDefaultSite =
        topSite is TopSite.Pinned || topSite is TopSite.DefaultPinned || topSite is TopSite.Default
    val isProvidedSite = topSite is TopSite.Provided
    val isFrecentSite = topSite is TopSite.Frecent
    val result = mutableListOf<MenuItem>()

    result.add(
        MenuItem(
            title = stringResource(id = R.string.bookmark_menu_open_in_private_tab_button),
            testTag = TopSitesTestTag.OPEN_IN_PRIVATE_TAB,
            onClick = { onOpenInPrivateTabClicked(topSite) },
        )
    )

    if (isPinnedOrDefaultSite || isFrecentSite) {
        result.add(
            MenuItem(
                title = stringResource(id = R.string.top_sites_edit_top_site),
                testTag = TopSitesTestTag.EDIT,
                onClick = { onEditTopSiteClicked(topSite) },
            )
        )
    }

    if (!isProvidedSite) {
        result.add(
            MenuItem(
                title =
                    stringResource(
                        id =
                            if (isPinnedOrDefaultSite) {
                                R.string.remove_top_site
                            } else {
                                R.string.delete_from_history
                            }
                    ),
                testTag = TopSitesTestTag.REMOVE,
                onClick = { onRemoveTopSiteClicked(topSite) },
            )
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
            )
        )
    }

    return result
}

/** Returns the maximum number of lines for a top site title based on its type. */
private fun TopSite.getMaxLinesForTitle(): Int {
    return if (this is TopSite.Provided) 1 else 2
}

@FlexibleWindowPreview
@Composable
private fun TopSitesPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        Surface {
            Box(modifier = Modifier.padding(all = FirefoxTheme.layout.space.static200)) {
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
                    onAddShortcutClicked = {},
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun ShowMoreExperimentPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        Surface {
            Box(modifier = Modifier.padding(all = FirefoxTheme.layout.space.static200)) {
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
                    onAddShortcutClicked = {},
                    isExpandToggleEnabled = true,
                )
            }
        }
    }
}
