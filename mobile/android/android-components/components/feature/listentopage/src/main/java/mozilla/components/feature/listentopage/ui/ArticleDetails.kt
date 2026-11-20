/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

/**
 * What the player says about the article it reads.
 *
 * @property title The article title, or `null` when the page has none.
 * @property site The site the article is from, shown under the title.
 * @property url The article's url, shown in full in place of the title when there is no title.
 */
data class ArticleDetails(val title: String? = null, val site: String? = null, val url: String? = null) {
    internal val hasTitle: Boolean
        get() = !title.isNullOrBlank()

    internal val heading: String?
        get() = if (hasTitle) title else url
}
