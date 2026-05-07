/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.grouptabstrip

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.theme.FirefoxTheme
import mozilla.components.ui.icons.R as iconsR

private val stripHeight = 44.dp
private val tabItemHeight = 36.dp
private val tabItemMinWidth = 120.dp
private val tabItemMaxWidth = 200.dp
private val faviconSize = 18.dp
private val groupAccentSize = 10.dp
private val tabSpacing = 6.dp

/**
 * Bottom strip showing the tabs that belong to the same group as the active tab.
 * Hidden by passing `state = null`.
 *
 * @param state The current [GroupTabStripState] to render, or null to render an empty placeholder.
 * @param onSelectTab Invoked with the tab id when a tab card is clicked.
 * @param onCloseTab Invoked with the tab id when the close button on a tab is clicked.
 * @param onAddTabInGroup Invoked when the trailing "+" is clicked, with the active group id.
 * @param onShowGroup Invoked when the group title chip is clicked, with the active group id.
 */
@Composable
fun GroupTabStrip(
    state: GroupTabStripState?,
    onSelectTab: (tabId: String) -> Unit,
    onCloseTab: (tabId: String) -> Unit,
    onAddTabInGroup: (groupId: String) -> Unit,
    onShowGroup: (groupId: String) -> Unit,
) {
    if (state == null) return

    FirefoxTheme {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(stripHeight)
                .background(MaterialTheme.colorScheme.surface)
                .padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            GroupChip(
                title = state.groupTitle,
                accentColor = state.groupTheme.primary,
                onClick = { onShowGroup(state.groupId) },
            )

            Spacer(modifier = Modifier.width(8.dp))

            val listState = rememberLazyListState()

            LaunchedEffect(state.tabs.firstOrNull { it.isSelected }?.id) {
                val selectedIndex = state.tabs.indexOfFirst { it.isSelected }
                if (selectedIndex >= 0) {
                    listState.animateScrollToItem(selectedIndex)
                }
            }

            LazyRow(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight(),
                state = listState,
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(tabSpacing),
            ) {
                items(items = state.tabs, key = { it.id }) { tab ->
                    GroupTabCard(
                        tab = tab,
                        accentColor = state.groupTheme.primary,
                        onClick = { onSelectTab(tab.id) },
                        onClose = { onCloseTab(tab.id) },
                    )
                }
            }

            Spacer(modifier = Modifier.width(4.dp))

            IconButton(
                onClick = { onAddTabInGroup(state.groupId) },
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_plus_24),
                    tint = MaterialTheme.colorScheme.onSurface,
                    contentDescription = stringResource(R.string.group_tab_strip_add_tab),
                )
            }
        }
    }
}

@Composable
private fun GroupChip(
    title: String,
    accentColor: Color,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .height(tabItemHeight)
            .clip(RoundedCornerShape(50))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(groupAccentSize)
                .clip(CircleShape)
                .background(accentColor),
        )
        if (title.isNotBlank()) {
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = title,
                color = MaterialTheme.colorScheme.onSurface,
                style = FirefoxTheme.typography.subtitle2,
                maxLines = 1,
            )
        }
    }
}

@Composable
private fun GroupTabCard(
    tab: GroupTabStripItem,
    accentColor: Color,
    onClick: () -> Unit,
    onClose: () -> Unit,
) {
    val backgroundColor = if (tab.isSelected) {
        MaterialTheme.colorScheme.surfaceVariant
    } else {
        MaterialTheme.colorScheme.surface
    }

    Row(
        modifier = Modifier
            .height(tabItemHeight)
            .widthIn(min = tabItemMinWidth, max = tabItemMaxWidth)
            .clip(RoundedCornerShape(8.dp))
            .background(backgroundColor)
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (tab.isSelected) {
            Box(
                modifier = Modifier
                    .size(width = 3.dp, height = faviconSize)
                    .background(accentColor),
            )
            Spacer(modifier = Modifier.width(6.dp))
        }

        TabFavicon(url = tab.url, icon = tab.icon)

        Spacer(modifier = Modifier.width(8.dp))

        Text(
            text = tab.title,
            modifier = Modifier.weight(1f),
            color = MaterialTheme.colorScheme.onSurface,
            style = FirefoxTheme.typography.caption,
            maxLines = 1,
            softWrap = false,
        )

        IconButton(
            onClick = onClose,
            modifier = Modifier.size(24.dp),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_cross_20),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                contentDescription = stringResource(
                    id = R.string.group_tab_strip_close_tab,
                    tab.title,
                ),
            )
        }
    }
}

@Composable
private fun TabFavicon(url: String, icon: Bitmap?) {
    Box(
        modifier = Modifier
            .size(faviconSize)
            .clip(CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        if (icon != null && !icon.isRecycled) {
            Image(
                bitmap = icon.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier
                    .size(faviconSize)
                    .clip(CircleShape),
            )
        } else if (url == ABOUT_HOME_URL) {
            Favicon(
                imageResource = R.drawable.ic_firefox,
                size = faviconSize,
            )
        } else {
            Favicon(
                url = url,
                size = faviconSize,
            )
        }
    }
}

