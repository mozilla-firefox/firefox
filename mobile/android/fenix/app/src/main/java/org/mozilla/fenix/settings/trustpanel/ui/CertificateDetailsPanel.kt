/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.MenuScaffold
import org.mozilla.fenix.theme.FirefoxTheme

private const val DARK_BACKGROUND_LUMINANCE = 0.5f

// The EU trust mark must be displayed at least 64 pixels wide and 85 pixels high, as required by Commission
// Implementing Regulation (EU) 2015/806. With the trust mark's aspect ratio, this width gives a height of about 94dp.
private val EU_TRUST_MARK_WIDTH = 64.dp

/**
 * Sub-page of the Trust Panel that shows the certificate details of the current website.
 *
 * @param url The URL of the current website.
 * @param icon The favicon of the current website, or null to fall back to a generated icon.
 * @param isQwac Whether the certificate is a Qualified Website Authentication Certificate (QWAC), in which case the
 *   QWAC statement and the EU trust mark are shown.
 * @param issuedTo Slot for the details of who the certificate was issued to.
 * @param verifiedBy Slot for the details of who verified the certificate.
 */
@Composable
internal fun CertificateDetailsPanel(
    url: String,
    icon: Bitmap?,
    isQwac: Boolean,
    issuedTo: @Composable () -> Unit,
    verifiedBy: @Composable () -> Unit,
) {
    MenuScaffold(header = { CertificateDetailsHeader(url = url, icon = icon) }) {
        Text(
            text = stringResource(R.string.certificate_details_panel_secure_connection_title),
            modifier =
                Modifier.fillMaxWidth()
                    .padding(
                        horizontal = FirefoxTheme.layout.space.static200,
                        vertical = FirefoxTheme.layout.space.static100,
                    )
                    .semantics { heading() },
            color = MaterialTheme.colorScheme.tertiary,
            style = FirefoxTheme.typography.headline8,
        )

        Column(
            modifier =
                Modifier.fillMaxWidth()
                    .clip(shape = MaterialTheme.shapes.large)
                    .background(color = MaterialTheme.colorScheme.surfaceBright)
                    .padding(
                        horizontal = FirefoxTheme.layout.space.static200,
                        vertical = FirefoxTheme.layout.space.static150,
                    ),
            verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
        ) {
            issuedTo()

            Row(verticalAlignment = Alignment.Bottom) {
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
                ) {
                    verifiedBy()

                    if (isQwac) {
                        Text(
                            text = stringResource(R.string.connection_security_panel_qualified_certificate),
                            color = MaterialTheme.colorScheme.onSurface,
                            style = FirefoxTheme.typography.body2,
                        )
                    }
                }

                if (isQwac) {
                    Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static200))

                    EuTrustMark()
                }
            }
        }
    }
}

@Composable
private fun EuTrustMark() {
    // The colors of the EU trust mark must not be altered, so the inverted asset is used on dark backgrounds instead
    // of tinting it.
    val isDarkBackground = MaterialTheme.colorScheme.surfaceBright.luminance() < DARK_BACKGROUND_LUMINANCE
    Image(
        painter =
            painterResource(if (isDarkBackground) R.drawable.eu_trust_mark_inverted else R.drawable.eu_trust_mark),
        contentDescription = stringResource(R.string.certificate_details_panel_eu_trust_mark_content_description),
        modifier = Modifier.width(EU_TRUST_MARK_WIDTH),
    )
}

@Composable
private fun CertificateDetailsHeader(
    url: String,
    icon: Bitmap?,
) {
    Row(
        modifier =
            Modifier.fillMaxWidth()
                .padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = FirefoxTheme.layout.space.static100,
                ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProtectionPanelIcon(url = url, icon = icon)

        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static200))

        Text(
            text = url.tryGetHostFromUrl(),
            modifier = Modifier.weight(1f).semantics { heading() },
            color = MaterialTheme.colorScheme.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = FirefoxTheme.typography.headline7,
        )
    }
}

@Composable
private fun CertificateDetailsPanelPreviewContent(
    url: String = "https://www.theguardian.com",
    isQwac: Boolean = true,
) {
    Column(modifier = Modifier.background(color = MaterialTheme.colorScheme.surface)) {
        CertificateDetailsPanel(
            url = url,
            icon = null,
            isQwac = isQwac,
            issuedTo = {
                Column {
                    Text(
                        text = "Certificate issued to:",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        style = FirefoxTheme.typography.caption,
                    )
                    Text(
                        text = "The Guardian\nLondon\nUK",
                        color = MaterialTheme.colorScheme.onSurface,
                        style = FirefoxTheme.typography.body2,
                    )
                }
            },
            verifiedBy = {
                Column {
                    Text(
                        text = "Verified by:",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        style = FirefoxTheme.typography.caption,
                    )
                    Text(
                        text = "GlobalSign nv-sa",
                        color = MaterialTheme.colorScheme.onSurface,
                        style = FirefoxTheme.typography.body2,
                    )
                }
            },
        )
    }
}

@FlexibleWindowPreview
@Composable
private fun CertificateDetailsPanelPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) { CertificateDetailsPanelPreviewContent() }
}

@Preview
@Composable
private fun CertificateDetailsPanelNotQwacPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) { CertificateDetailsPanelPreviewContent(isQwac = false) }
}

@Preview
@Composable
private fun CertificateDetailsPanelLongDomainPreview() {
    FirefoxTheme {
        CertificateDetailsPanelPreviewContent(
            url = "https://a-very-long-subdomain.of-an-even-longer-domain-name.example.org"
        )
    }
}

@Preview(fontScale = 2f)
@Composable
private fun CertificateDetailsPanelLargeFontPreview() {
    FirefoxTheme { CertificateDetailsPanelPreviewContent() }
}

@Preview(locale = "ar")
@Composable
private fun CertificateDetailsPanelRtlPreview() {
    FirefoxTheme { CertificateDetailsPanelPreviewContent() }
}
