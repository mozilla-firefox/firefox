/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import java.util.IllformedLocaleException
import java.util.Locale
import org.mozilla.fenix.settings.trustpanel.store.CertificateDetails

/**
 * The subject fields of a certificate, as displayed by [CertificateIssuedTo].
 *
 * @property organization The organization the certificate was issued to, or null if absent.
 * @property locality The locality of the organization, or null if absent.
 * @property country The display name of the organization's country, or null if absent.
 */
internal data class IssuedTo(
    val organization: String?,
    val locality: String?,
    val country: String?,
)

/**
 * Maps the subject fields of these [CertificateDetails] to [IssuedTo], with the country code converted to its name in
 * [locale]. Blank fields are treated as absent.
 *
 * Until the missing-field behaviour is decided, absent fields are left out and null is returned when all of them are
 * absent, so that no blank label or empty row is shown.
 */
internal fun CertificateDetails?.toIssuedTo(locale: Locale): IssuedTo? {
    val organization = this?.subjectOrganization?.takeIf { it.isNotBlank() }
    val locality = this?.subjectLocality?.takeIf { it.isNotBlank() }
    val countryCode = this?.subjectCountry?.takeIf { it.isNotBlank() }

    if (organization == null && locality == null && countryCode == null) return null

    return IssuedTo(
        organization = organization,
        locality = locality,
        country = countryCode?.let { countryDisplayName(it, locale) },
    )
}

/** Returns the name of the country with the given ISO 3166 [code] in [locale], or the code itself if it is unknown. */
internal fun countryDisplayName(code: String, locale: Locale): String {
    val country =
        try {
            Locale.Builder().setRegion(code).build()
        } catch (_: IllformedLocaleException) {
            return code
        }

    return country.getDisplayCountry(locale).takeIf { it.isNotBlank() } ?: code
}
