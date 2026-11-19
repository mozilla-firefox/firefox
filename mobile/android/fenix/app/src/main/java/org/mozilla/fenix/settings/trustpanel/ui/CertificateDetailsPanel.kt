/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

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
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.max
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.utils.CertificateDetails
import mozilla.components.support.utils.R as supportUtilsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.MenuScaffold
import org.mozilla.fenix.components.menu.compose.header.SubmenuHeader
import org.mozilla.fenix.theme.FirefoxTheme

private const val DARK_BACKGROUND_LUMINANCE = 0.5f

// The design shows the EU trust mark 58dp wide, which with its aspect ratio is 85dp high. It must also be displayed
// at least 64 pixels wide and 85 pixels high, as required by Commission Implementing Regulation (EU) 2015/806, so on
// low density screens it is widened to 64 pixels, which keeps it more than 85 pixels high.
private val EU_TRUST_MARK_WIDTH = 58.dp
private const val EU_TRUST_MARK_MIN_WIDTH_PX = 64

/**
 * Sub-page of the Trust Panel that shows the certificate details of the current website.
 *
 * @param url The URL of the current website.
 * @param isQwac Whether the certificate is a Qualified Website Authentication Certificate (QWAC), in which case the
 *   QWAC statement and the EU trust mark are shown.
 * @param issuedTo Slot for the details of who the certificate was issued to.
 * @param verifiedBy Slot for the details of who verified the certificate.
 * @param onBackButtonClick Invoked when the back button in the header is clicked.
 */
@Composable
internal fun CertificateDetailsPanel(
    url: String,
    isQwac: Boolean,
    issuedTo: @Composable () -> Unit,
    verifiedBy: @Composable () -> Unit,
    onBackButtonClick: () -> Unit,
) {
    MenuScaffold(
        header = {
            SubmenuHeader(
                title = "",
                url = url,
                backButtonContentDescription = stringResource(R.string.etp_back_button_content_description),
                onClick = onBackButtonClick,
            )
        }
    ) {
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
    val trustMark =
        if (isDarkBackground) {
            supportUtilsR.drawable.eu_trust_mark_inverted
        } else {
            supportUtilsR.drawable.eu_trust_mark
        }
    val minWidth = with(LocalDensity.current) { EU_TRUST_MARK_MIN_WIDTH_PX.toDp() }
    Image(
        painter = painterResource(trustMark),
        contentDescription = stringResource(R.string.certificate_details_panel_eu_trust_mark_content_description),
        modifier = Modifier.width(max(EU_TRUST_MARK_WIDTH, minWidth)),
    )
}

private val previewCertificateDetails =
    CertificateDetails(
        subjectOrganization = "The Guardian",
        subjectLocality = "London",
        subjectCountry = "GB",
        subjectCommonName = "www.theguardian.com",
        issuerOrganization = "GlobalSign nv-sa",
        issuerCommonName = "GlobalSign GCC R3 OV TLS CA 2024",
    )

@Composable
private fun CertificateDetailsPanelPreviewContent(
    url: String = "https://www.theguardian.com",
    isQwac: Boolean = true,
    certificateDetails: CertificateDetails = previewCertificateDetails,
) {
    Column(modifier = Modifier.background(color = MaterialTheme.colorScheme.surface)) {
        CertificateDetailsPanel(
            url = url,
            isQwac = isQwac,
            issuedTo = { CertificateIssuedTo(certificateDetails = certificateDetails) },
            verifiedBy = { CertificateVerifiedBy(certificateDetails = certificateDetails) },
            onBackButtonClick = {},
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
private fun CertificateDetailsPanelDomainValidatedPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        CertificateDetailsPanelPreviewContent(
            url = "https://example.com",
            isQwac = false,
            certificateDetails =
                CertificateDetails(
                    subjectCommonName = "example.com",
                    issuerOrganization = "Let's Encrypt",
                    issuerCommonName = "R13",
                ),
        )
    }
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
