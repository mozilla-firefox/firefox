/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.pagesummaries

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertEquals
import mozilla.components.support.test.robolectric.testContext
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class PageSummariesSettingsSearchProviderTest {
    @Test
    fun `WHEN search items are requested THEN the page summaries settings are returned`() {
        val items = PageSummariesSettingsSearchProvider.getSearchItems(testContext)
        val keys = items.map { it.preferenceKey }

        // Then assert the keys represent the items returned
        assertEquals(
            expected =
                listOf(
                    "PAGE_SUMMARIES_FEATURE",
                    "PAGE_SUMMARIES_GESTURES",
                ),
            actual = keys,
            message = "Expected the search items to match the page summaries settings",
        )
    }
}
