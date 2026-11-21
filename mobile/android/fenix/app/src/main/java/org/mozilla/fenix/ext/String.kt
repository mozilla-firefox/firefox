/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import androidx.compose.runtime.Composable
import androidx.core.net.toUri
import mozilla.components.compose.base.utils.inComposePreview
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.base.utils.MAX_URI_LENGTH
import mozilla.components.support.ktx.android.net.isHttpOrHttps
import mozilla.components.support.ktx.kotlin.isIpv4OrIpv6
import mozilla.components.support.ktx.kotlin.toShortUrl
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import org.mozilla.fenix.components.components

/**
 * Shortens URLs to be more user friendly, by applying [String.toShortUrl] and making sure it's equal or below the
 * [MAX_URI_LENGTH].
 */
@Composable
fun String.toShortUrl(): String {
    // Truncate to MAX_URI_LENGTH to prevent the UI from locking up for
    // extremely large URLs such as data URIs or bookmarklets. The same
    // is done in the toolbar and awesomebar:
    // https://github.com/mozilla-mobile/fenix/issues/1824
    // https://github.com/mozilla-mobile/android-components/issues/6985
    return if (inComposePreview) {
        this.take(MAX_URI_LENGTH)
    } else {
        this.toShortUrl(components.publicSuffixList).take(MAX_URI_LENGTH)
    }
}

/**
 * Trims a URL string of its scheme and common prefixes.
 *
 * This is intended to act much like [PublicSuffixList.getPublicSuffixPlusOne()] but unlike that method, leaves the
 * path, anchor, etc intact.
 */
fun String.simplifiedUrl(): String {
    val afterScheme = this.substringAfter("://")
    for (prefix in listOf("www.", "m.", "mobile.")) {
        if (afterScheme.startsWith(prefix)) {
            return afterScheme.substring(prefix.length)
        }
    }
    return afterScheme
}

/**
 * Extracts a shortened or simplified URL from a given URL string. (e.g., "example.com" instead of
 * "https://www.example.com/long/path/to/file")
 *
 * This function handles both regular URLs and blob URLs. For regular URLs, it extracts the base domain. For blob URLs,
 * it extracts the base domain from the URL embedded within the blob URL.
 *
 * @param publicSuffixList The [PublicSuffixList] used to resolve the registrable domain.
 * @return The registrable domain (e.g., "mozilla.org") or the host if a registrable domain cannot be determined.
 */
internal suspend fun String.getBaseDomainUrl(publicSuffixList: PublicSuffixList): String {
    val host =
        when {
            this.startsWith("blob:") -> this.substringAfter("blob:").tryGetHostFromUrl()
            else -> this.tryGetHostFromUrl()
        }
    val registrableDomain = publicSuffixList.getPublicSuffixPlusOne(host).await()

    return registrableDomain ?: host
}

/**
 * Returns a label identifying where a download came from, for display in the downloads list.
 *
 * For http(s) URLs (including blob: URLs wrapping them) this is the registrable domain or IP address, e.g.
 * "https://www.mozilla.org/a/b" returns "mozilla.org". data: and blob: URLs without a trustworthy host return "(data)"
 * as a localized string. Anything else returns the host on fallback, or an empty string if no host can be determined.
 *
 * @param publicSuffixList The [PublicSuffixList] used to resolve the registrable domain.
 * @param dataLabel A localized label to display for data origin URLs - e.g. (data)
 * @return A url formatted for display for downloads.
 */
internal suspend fun String.toDownloadDisplayUrl(publicSuffixList: PublicSuffixList, dataLabel: String): String {
    val uri = removePrefix("blob:").toUri()
    val host = uri.host?.lowercase()?.takeIf { uri.isHttpOrHttps && it.isNotEmpty() }
    if (host?.removeSurrounding("[", "]")?.isIpv4OrIpv6() == true) {
        return host
    }
    val baseUrl = host?.let { publicSuffixList.getPublicSuffixPlusOne(host).await() ?: host }
    return baseUrl ?: toUri().scheme?.lowercase()?.takeIf { it == "data" || it == "blob" }?.let { dataLabel }.orEmpty()
}
