/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.content

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ContentProviderTest {
    @Test
    fun `that we can provide the page content`() = runTest {
        val content = ContentProvider.fromPage(
            { Result.success("This is the page content") },
            { Result.success(PageMetadata(wordCount = 500)) },
        ).getContent().getOrThrow()

        assertEquals("This is the page content", content.body)
        assertEquals(PageMetadata(wordCount = 500), content.metadata)
    }

    @Test
    fun `that if we fail to extract content we return a failure`() = runTest {
        val content = ContentProvider.fromPage(
            { Result.failure(PageContentExtractor.Exception()) },
            { Result.success(PageMetadata()) },
        ).getContent().exceptionOrNull()

        assertTrue(content is PageContentExtractor.Exception)
    }

    @Test
    fun `that if extracting page metadata fails we recover with default metadata`() = runTest {
        val content = ContentProvider.fromPage(
            { Result.success("This is the page content") },
            { Result.failure(IllegalStateException()) },
        ).getContent().getOrThrow()

        assertEquals("This is the page content", content.body)
        assertEquals(PageMetadata(), content.metadata)
    }
}
