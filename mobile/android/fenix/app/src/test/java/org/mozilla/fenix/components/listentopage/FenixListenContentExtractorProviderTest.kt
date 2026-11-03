/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import kotlinx.coroutines.test.runTest
import mozilla.components.feature.listentopage.content.Content
import mozilla.components.feature.listentopage.content.ContentExtractor
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

private const val TAB_ID = "tab-1"
private const val OTHER_TAB_ID = "tab-2"

class FenixListenContentExtractorProviderTest {

    private val content = Content(text = "Article text.", languageTag = "en-US")
    private val debugController = ListenDebugController()
    private val provider =
        FenixListenContentExtractorProvider(
            extractor = ContentExtractor { Result.success(content) },
            debugController = debugController,
        )

    @Test
    fun `WHEN no failure is forced THEN the real extractor reads the tab`() = runTest {
        assertEquals(content, provider.extractorFor(TAB_ID).getContent(TAB_ID).getOrNull())
    }

    @Test
    fun `WHEN extraction is made to fail for a tab THEN its extraction fails`() = runTest {
        debugController.failExtractionFor(TAB_ID)

        assertTrue(provider.extractorFor(TAB_ID).getContent(TAB_ID).isFailure)
    }

    @Test
    fun `WHEN extraction is made to fail for a tab THEN other tabs are still read`() = runTest {
        debugController.failExtractionFor(TAB_ID)

        assertEquals(content, provider.extractorFor(OTHER_TAB_ID).getContent(OTHER_TAB_ID).getOrNull())
    }
}
