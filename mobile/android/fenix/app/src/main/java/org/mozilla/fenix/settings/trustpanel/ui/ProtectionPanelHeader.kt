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
import androidx.compose.material.Icon
import androidx.compose.material.IconButton
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.Divider
import mozilla.components.compose.base.annotation.LightDarkPreview
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.Theme

private val ICON_SIZE = 16.dp
private val ICON_PADDING = 8.dp
private val OUTER_ICON_SHAPE = RoundedCornerShape(4.dp)
private val INNER_ICON_SHAPE = RoundedCornerShape(0.dp)

@Composable
internal fun ProtectionPanelHeader(
    icon: Bitmap?,
    websiteInfoState: WebsiteInfoState,
    onConnectionSecurityClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(start = 12.dp, end = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Spacer(modifier = Modifier.width(4.dp))

        ProtectionPanelIcon(url = websiteInfoState.websiteUrl, icon = icon)

        Spacer(modifier = Modifier.width(8.dp))

        Column(
            modifier = Modifier
                .padding(horizontal = 8.dp)
                .weight(1f),
        ) {
            Text(
                text = websiteInfoState.websiteTitle,
                color = FirefoxTheme.colors.textSecondary,
                maxLines = 1,
                style = FirefoxTheme.typography.headline7,
                overflow = TextOverflow.Ellipsis,
            )

            Text(
                text = websiteInfoState.websiteUrl.tryGetHostFromUrl(),
                color = FirefoxTheme.colors.textSecondary,
                maxLines = 1,
                style = FirefoxTheme.typography.caption,
            )
        }

        Spacer(modifier = Modifier.width(8.dp))

        Divider(modifier = Modifier.size(width = 2.dp, height = 32.dp))

        IconButton(
            modifier = Modifier.padding(horizontal = 10.dp),
            onClick = onConnectionSecurityClick,
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(
                    painter = if (websiteInfoState.isSecured) {
                        painterResource(id = R.drawable.mozac_ic_lock_20)
                    } else {
                        painterResource(id = R.drawable.mozac_ic_lock_slash_20)
                    },
                    contentDescription = null,
                    tint = FirefoxTheme.colors.iconSecondary,
                )

                Text(
                    text = if (websiteInfoState.isSecured) {
                        stringResource(id = R.string.protection_panel_header_secure)
                    } else {
                        stringResource(id = R.string.protection_panel_header_not_secure)
                    },
                    color = FirefoxTheme.colors.textSecondary,
                    maxLines = 1,
                    style = FirefoxTheme.typography.caption,
                )

                Spacer(modifier = Modifier.width(10.dp))

                Icon(
                    painter = painterResource(id = R.drawable.mozac_ic_chevron_right_24),
                    contentDescription = null,
                    tint = FirefoxTheme.colors.iconSecondary,
                )
            }
        }
    }
}

@Composable
private fun ProtectionPanelIcon(
    url: String,
    icon: Bitmap?,
) {
    if (icon != null && !icon.isRecycled) {
        Image(
            bitmap = icon.asImageBitmap(),
            contentDescription = null,
            modifier = Modifier
                .background(
                    color = FirefoxTheme.colors.layer2,
                    shape = OUTER_ICON_SHAPE,
                )
                .padding(all = ICON_PADDING)
                .size(ICON_SIZE),
        )
    } else {
        Favicon(
            url = url,
            modifier = Modifier
                .background(
                    color = FirefoxTheme.colors.layer2,
                    shape = OUTER_ICON_SHAPE,
                )
                .padding(all = ICON_PADDING),
            size = ICON_SIZE,
            roundedCornerShape = INNER_ICON_SHAPE,
        )
    }
}

@LightDarkPreview
@Composable
private fun ProtectionPanelHeaderPreview() {
    FirefoxTheme {
        Column(
            modifier = Modifier
                .background(color = FirefoxTheme.colors.layer3),
        ) {
            ProtectionPanelHeader(
                icon = null,
                websiteInfoState = WebsiteInfoState(
                    isSecured = true,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "Mozilla",
                    certificateName = "",
                ),
                onConnectionSecurityClick = {},
            )
        }
    }
}

@Preview
@Composable
private fun ProtectionPanelHeaderPrivatePreview() {
    FirefoxTheme(theme = Theme.Private) {
        Column(
            modifier = Modifier
                .background(color = FirefoxTheme.colors.layer3),
        ) {
            ProtectionPanelHeader(
                icon = null,
                websiteInfoState = WebsiteInfoState(
                    isSecured = false,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "Mozilla",
                    certificateName = "",
                ),
                onConnectionSecurityClick = {},
            )
        }
    }
}
