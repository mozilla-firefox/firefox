/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import java.security.cert.X509Certificate
import java.util.IllformedLocaleException
import java.util.Locale
import javax.security.auth.x500.X500Principal

/**
 * Subject and issuer fields of a website's certificate. Fields missing from the certificate are null.
 *
 * @property subjectOrganization The organization (O) the certificate was issued to.
 * @property subjectLocality The locality (L) of the organization the certificate was issued to.
 * @property subjectCountry The country (C) of the organization the certificate was issued to.
 * @property subjectCommonName The common name (CN) the certificate was issued to.
 * @property issuerOrganization The organization (O) of the certificate issuer.
 * @property issuerCommonName The common name (CN) of the certificate issuer.
 */
data class CertificateDetails(
    val subjectOrganization: String? = null,
    val subjectLocality: String? = null,
    val subjectCountry: String? = null,
    val subjectCommonName: String? = null,
    val issuerOrganization: String? = null,
    val issuerCommonName: String? = null,
) {
    /**
     * The subject fields of a certificate, as shown to the user under "Certificate issued to".
     *
     * @property organization The organization the certificate was issued to, or null if absent.
     * @property locality The locality of the organization, or null if absent.
     * @property country The display name of the organization's country, or null if absent.
     */
    data class IssuedTo(
        val organization: String?,
        val locality: String?,
        val country: String?,
    )
}

/** Returns the subject and issuer fields of this certificate. */
fun X509Certificate.toCertificateDetails(): CertificateDetails {
    val subject = parseDistinguishedName(subjectX500Principal.getName(X500Principal.RFC2253))
    val issuer = parseDistinguishedName(issuerX500Principal.getName(X500Principal.RFC2253))

    return CertificateDetails(
        subjectOrganization = subject["O"],
        subjectLocality = subject["L"],
        subjectCountry = subject["C"],
        subjectCommonName = subject["CN"],
        issuerOrganization = issuer["O"],
        issuerCommonName = issuer["CN"],
    )
}

/**
 * Maps the subject fields of these [CertificateDetails] to [CertificateDetails.IssuedTo], with the country code
 * converted to its name in [locale]. Blank fields are treated as absent.
 *
 * Absent fields are left out, and null is returned if all of them are absent, so that no blank label or empty row is
 * shown.
 */
fun CertificateDetails?.toIssuedTo(locale: Locale): CertificateDetails.IssuedTo? {
    val organization = this?.subjectOrganization?.takeIf { it.isNotBlank() }
    val locality = this?.subjectLocality?.takeIf { it.isNotBlank() }
    val countryCode = this?.subjectCountry?.takeIf { it.isNotBlank() }

    if (organization == null && locality == null && countryCode == null) return null

    return CertificateDetails.IssuedTo(
        organization = organization,
        locality = locality,
        country = countryCode?.let { countryDisplayName(it, locale) },
    )
}

/**
 * Returns the name of the certificate authority that issued the certificate: its organization, or its common name if it
 * has no organization. Blank fields are treated as absent.
 *
 * Null is returned when neither is present, so that no dangling label is shown.
 */
fun CertificateDetails?.issuerDisplayName(): String? =
    this?.issuerOrganization?.takeIf { it.isNotBlank() } ?: this?.issuerCommonName?.takeIf { it.isNotBlank() }

/**
 * Whether these [CertificateDetails] have anything to show: who the certificate was issued to, or who issued it. When
 * they have neither, the certificate details are not offered at all.
 */
fun CertificateDetails?.hasDisplayableDetails(): Boolean =
    toIssuedTo(Locale.ROOT) != null || issuerDisplayName() != null

/**
 * Parses an RFC 2253 distinguished name into a map of attribute types to values. Only the first value of each type is
 * kept.
 *
 * For example, `CN=www.example.com,O=Example\, Inc.,L=London,C=GB` is parsed into:
 * ```
 * {CN=www.example.com, O=Example, Inc., L=London, C=GB}
 * ```
 */
internal fun parseDistinguishedName(distinguishedName: String): Map<String, String> {
    val attributes = mutableMapOf<String, String>()
    var type: String? = null
    val value = StringBuilder()
    var isEscaped = false

    fun addAttribute() {
        val text = value.trim().toString()
        type?.let { if (text.isNotEmpty()) attributes.putIfAbsent(it.trim().uppercase(), text) }
        type = null
        value.clear()
    }

    for (char in distinguishedName) {
        when {
            isEscaped -> {
                value.append(char)
                isEscaped = false
            }
            char == '\\' -> isEscaped = true
            char == '=' && type == null -> {
                type = value.toString()
                value.clear()
            }
            char == ',' || char == '+' -> addAttribute()
            else -> value.append(char)
        }
    }
    addAttribute()

    return attributes
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
