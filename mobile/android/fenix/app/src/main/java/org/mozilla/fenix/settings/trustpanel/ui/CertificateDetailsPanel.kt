/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.graphics.Bitmap
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.MenuScaffold
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Sub-page of the Trust Panel that shows the certificate details of the current website.
 *
 * @param url The URL of the current website.
 * @param icon The favicon of the current website, or null to fall back to a generated icon.
 * @param issuedTo Slot for the details of who the certificate was issued to.
 * @param verifiedBy Slot for the details of who verified the certificate.
 */
@Composable
internal fun CertificateDetailsPanel(
    url: String,
    icon: Bitmap?,
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
            verifiedBy()
        }
    }
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
private fun CertificateDetailsPanelPreviewContent(url: String = "https://www.theguardian.com") {
    Column(modifier = Modifier.background(color = MaterialTheme.colorScheme.surface)) {
        CertificateDetailsPanel(
            url = url,
            icon = null,
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
