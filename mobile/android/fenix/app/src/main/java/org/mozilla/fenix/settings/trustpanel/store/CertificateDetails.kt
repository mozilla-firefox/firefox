/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.store

import java.security.cert.X509Certificate
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
)

internal fun X509Certificate.toCertificateDetails(): CertificateDetails {
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
