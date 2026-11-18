/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.activity

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import androidx.core.net.toUri
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.feature.search.ext.buildSearchUrl
import mozilla.components.feature.search.ext.waitForSelectedOrDefaultSearchEngine
import mozilla.components.support.utils.SafeIntent
import org.mozilla.focus.ext.components

/** Activity for receiving and processing an ACTION_PROCESS_TEXT intent. */
class TextActionActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val intent = SafeIntent(intent)

        val searchTextCharSequence = intent.getCharSequenceExtra(Intent.EXTRA_PROCESS_TEXT)
        val searchText = searchTextCharSequence?.toString() ?: ""

        components.store.waitForSelectedOrDefaultSearchEngine { searchEngine ->
            val searchUrl = searchEngine?.buildSearchUrl(searchText, SearchAccessPoint.CONTEXT).toString()
            val searchIntent = Intent(this, IntentReceiverActivity::class.java)
            searchIntent.action = Intent.ACTION_VIEW
            searchIntent.putExtra(EXTRA_TEXT_SELECTION, true)
            searchIntent.data = searchUrl.toUri()
            searchIntent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP

            startActivity(searchIntent)

            finish()
        }
    }

    companion object {
        const val EXTRA_TEXT_SELECTION = "text_selection"
    }
}
