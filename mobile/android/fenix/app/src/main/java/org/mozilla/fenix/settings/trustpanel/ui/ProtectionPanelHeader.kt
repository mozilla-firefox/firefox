/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.theme.FirefoxTheme

private val ICON_SIZE = 16.dp
private val ICON_PADDING = 8.dp
private val INNER_ICON_SHAPE = RoundedCornerShape(0.dp)

@Composable
internal fun ProtectionPanelHeader(
    websiteInfoState: WebsiteInfoState,
    icon: Bitmap?,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProtectionPanelIcon(url = websiteInfoState.websiteUrl, icon = icon)

        Spacer(modifier = Modifier.width(16.dp))

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text =
                    websiteInfoState.websiteTitle.ifEmpty {
                        websiteInfoState.websiteUrl.tryGetHostFromUrl()
                    },
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                style = FirefoxTheme.typography.headline7,
                overflow = TextOverflow.Ellipsis,
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = "unified.trust.panel.website"
                    },
            )

            if (websiteInfoState.websiteTitle.isNotEmpty()) {
                Text(
                    text = websiteInfoState.websiteUrl.tryGetHostFromUrl(),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = FirefoxTheme.typography.body2,
                    modifier =
                        Modifier.semantics {
                            testTagsAsResourceId = true
                            testTag = "unified.trust.panel.website.url"
                        },
                )
            }
        }
    }
}

@Composable
internal fun ProtectionPanelIcon(
    url: String,
    icon: Bitmap?,
) {
    if (icon != null && !icon.isRecycled) {
        Image(
            bitmap = icon.asImageBitmap(),
            contentDescription = null,
            modifier =
                Modifier.background(
                        color = MaterialTheme.colorScheme.surfaceContainerLowest,
                        shape = MaterialTheme.shapes.extraSmall,
                    )
                    .padding(all = ICON_PADDING)
                    .size(ICON_SIZE),
        )
    } else {
        Favicon(
            url = url,
            modifier =
                Modifier.background(
                        color = MaterialTheme.colorScheme.surfaceContainerLowest,
                        shape = MaterialTheme.shapes.extraSmall,
                    )
                    .padding(all = ICON_PADDING),
            size = ICON_SIZE,
            shape = INNER_ICON_SHAPE,
        )
    }
}

@Preview
@Composable
private fun ProtectionPanelHeaderPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        ProtectionPanelHeader(
            websiteInfoState =
                WebsiteInfoState(
                    isSecured = true,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "Mozilla",
                    certificate = null,
                ),
            icon = null,
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surface),
        )
    }
}

@Preview
@Composable
private fun ProtectionPanelHeaderUrlAsTitlePreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        ProtectionPanelHeader(
            websiteInfoState =
                WebsiteInfoState(
                    isSecured = true,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "",
                    certificate = null,
                ),
            icon = null,
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surface),
        )
    }
}
