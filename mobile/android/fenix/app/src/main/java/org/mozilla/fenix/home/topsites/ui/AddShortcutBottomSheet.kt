/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import mozilla.components.compose.base.BottomSheetHandle
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.MenuGroup
import org.mozilla.fenix.components.menu.compose.MenuItem
import org.mozilla.fenix.compose.rememberModalSheetState
import org.mozilla.fenix.home.topsites.TopSitesTestTag
import org.mozilla.fenix.home.topsites.store.PopularSite
import org.mozilla.fenix.theme.FirefoxTheme

private const val PREVIEW_POPULAR_SITE_COUNT = 8

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun AddShortcutBottomSheet(
    popularSites: List<PopularSite>,
    onDismiss: () -> Unit,
    onAddWebsiteClicked: () -> Unit,
    onAddPopularSiteClick: (PopularSite) -> Unit,
) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MaterialTheme.colorScheme.surface,
        dragHandle = {
            BottomSheetHandle(
                onRequestDismiss = onDismiss,
                contentDescription = stringResource(R.string.homepage_shortcuts_add_to_homepage),
                modifier = Modifier.padding(all = FirefoxTheme.layout.space.static200),
            )
        },
    ) {
        AddShortcutBottomSheetContent(
            popularSites = popularSites,
            onAddWebsiteClicked = onAddWebsiteClicked,
            onAddPopularSiteClick = onAddPopularSiteClick,
        )
    }
}

@Composable
private fun AddShortcutBottomSheetContent(
    popularSites: List<PopularSite>,
    onAddWebsiteClicked: () -> Unit,
    onAddPopularSiteClick: (PopularSite) -> Unit,
) {
    Column(
        modifier =
            Modifier.verticalScroll(rememberScrollState()).padding(horizontal = FirefoxTheme.layout.space.static200)
    ) {
        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static150))

        Text(
            text = stringResource(R.string.homepage_shortcuts_add_to_homepage),
            style = FirefoxTheme.typography.headline7,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth(),
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        MenuGroup {
            MenuItem(
                label = stringResource(id = R.string.homepage_shortcuts_add_website),
                beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_globe_24),
                onClick = onAddWebsiteClicked,
                modifier = Modifier.testTag(TopSitesTestTag.ADD_WEBSITE),
            )
        }

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        PopularSites(
            sites = popularSites,
            onClick = onAddPopularSiteClick,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
    }
}

@Composable
@FlexibleWindowLightDarkPreview
private fun AddShortcutBottomSheetPreview() {
    FirefoxTheme {
        Surface {
            AddShortcutBottomSheetContent(
                popularSites =
                    List(PREVIEW_POPULAR_SITE_COUNT) {
                        PopularSite(title = "Mozilla", url = "https://mozilla.com", iconUrl = null)
                    },
                onAddWebsiteClicked = {},
                onAddPopularSiteClick = {},
            )
        }
    }
}
