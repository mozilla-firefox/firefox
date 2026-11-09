/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.trustpanel.store.CertificateDetails
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Shows who the website's certificate was issued to: the organization, its locality and its country. Nothing is shown
 * when all of them are absent.
 *
 * @param certificateDetails The certificate details of the current website, or null if there is no certificate.
 * @param modifier [Modifier] to be applied to the layout.
 */
@Composable
internal fun CertificateIssuedTo(
    certificateDetails: CertificateDetails?,
    modifier: Modifier = Modifier,
) {
    val issuedTo = certificateDetails.toIssuedTo(LocalConfiguration.current.locales[0]) ?: return

    Column(modifier = modifier.semantics(mergeDescendants = true) {}) {
        Text(
            text = stringResource(R.string.certificate_details_panel_issued_to_label),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = FirefoxTheme.typography.caption,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static50))

        issuedTo.organization?.let {
            Text(
                text = it,
                color = MaterialTheme.colorScheme.onSurface,
                style = FirefoxTheme.typography.subtitle2,
            )
        }

        listOfNotNull(issuedTo.locality, issuedTo.country).forEach {
            Text(
                text = it,
                color = MaterialTheme.colorScheme.onSurface,
                style = FirefoxTheme.typography.body2,
            )
        }
    }
}

@Composable
private fun CertificateIssuedToPreviewContent(certificateDetails: CertificateDetails) {
    CertificateIssuedTo(
        certificateDetails = certificateDetails,
        modifier =
            Modifier.background(color = MaterialTheme.colorScheme.surfaceBright)
                .padding(all = FirefoxTheme.layout.space.static200),
    )
}

@Preview
@Composable
private fun CertificateIssuedToPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        CertificateIssuedToPreviewContent(
            CertificateDetails(
                subjectOrganization = "The Guardian",
                subjectLocality = "London",
                subjectCountry = "GB",
            )
        )
    }
}

@Preview
@Composable
private fun CertificateIssuedToMissingFieldsPreview() {
    FirefoxTheme { CertificateIssuedToPreviewContent(CertificateDetails(subjectOrganization = "Mozilla Corporation")) }
}

@Preview(fontScale = 2f)
@Composable
private fun CertificateIssuedToLongValuesPreview() {
    FirefoxTheme {
        CertificateIssuedToPreviewContent(
            CertificateDetails(
                subjectOrganization = "A Very Long Organization Name That Does Not Fit On A Single Line Ltd",
                subjectLocality = "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch",
                subjectCountry = "GB",
            )
        )
    }
}

@Preview(locale = "ar")
@Composable
private fun CertificateIssuedToRtlPreview() {
    FirefoxTheme {
        CertificateIssuedToPreviewContent(
            CertificateDetails(
                subjectOrganization = "The Guardian",
                subjectLocality = "London",
                subjectCountry = "GB",
            )
        )
    }
}
