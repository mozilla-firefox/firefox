/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.utils

import android.content.Context
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.feature.search.ext.buildSearchUrl
import org.mozilla.focus.ext.components

/** Utility for search-related operations. */
object SearchUtils {
    /** Creates a search URL for the given [text] using the selected search engine. */
    fun createSearchUrl(context: Context?, text: String, accessPoint: String = SearchAccessPoint.DEFAULT): String {
        val searchEngine = context?.components?.store?.state?.search?.selectedOrDefaultSearchEngine ?: return text

        return searchEngine.buildSearchUrl(text, accessPoint)
    }
}
