/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose.header

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.components.menu.MenuTestTag
import org.mozilla.fenix.theme.FirefoxTheme

@Composable
internal fun SubmenuHeader(
    title: String,
    url: String,
    backButtonContentDescription: String? = null,
    onClick: () -> Unit,
) {
    Row(
        modifier =
            Modifier.padding(start = FirefoxTheme.layout.space.static50, end = FirefoxTheme.layout.space.static200)
                .defaultMinSize(minHeight = 56.dp)
                .verticalScroll(rememberScrollState()),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
            IconButton(
                onClick = { onClick() },
                contentDescription = backButtonContentDescription,
                modifier = Modifier.testTag(MenuTestTag.SUBMENU_HEADER_BACK_BUTTON),
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }

            Spacer(modifier = Modifier.width(4.dp))

            Column {
                Text(
                    text = title.ifEmpty { url.tryGetHostFromUrl() },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.semantics { heading() },
                    style = FirefoxTheme.typography.headline7,
                )
                if (title.isNotEmpty()) {
                    Text(
                        text = url.tryGetHostFromUrl(),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        style = FirefoxTheme.typography.body2,
                    )
                }
            }
        }
    }
}

@Preview
@Composable
private fun SubmenuHeaderPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        Column(modifier = Modifier.background(color = MaterialTheme.colorScheme.surface)) {
            SubmenuHeader(
                title = "sub-menu title",
                url = "https://www.mozilla.com",
                onClick = {},
            )
            HorizontalDivider()
            SubmenuHeader(
                title = "",
                url = "https://www.mozilla.com",
                onClick = {},
            )
            HorizontalDivider()
            SubmenuHeader(
                title = "Extra long long long long long long long long long long title",
                url = "https://www.mozilla.com",
                onClick = {},
            )
        }
    }
}
