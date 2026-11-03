/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.content

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ContentExtractorTest {
    @Test
    fun `test that fromPage combines the article text and its language`() = runTest {
        val provider =
            ContentExtractor.fromPage(
                pageContentExtractor = { Result.success("Article text") },
                pageMetadataExtractor = {
                    Result.success(PageMetadata(languageTag = "de"))
                },
                fallbackLanguageTag = { "en-GB" },
            )

        val content = provider.getContent("tab-1").getOrThrow()

        assertEquals("Article text", content.text)
        assertEquals("de", content.languageTag)
        assertEquals(LanguageSource.Page, content.languageSource)
    }

    @Test
    fun `test that the declared language is normalized`() = runTest {
        val provider =
            ContentExtractor.fromPage(
                pageContentExtractor = { Result.success("Article text") },
                pageMetadataExtractor = { Result.success(PageMetadata(languageTag = " de_DE ")) },
                fallbackLanguageTag = { "en-US" },
            )

        assertEquals("de-DE", provider.getContent("tab-1").getOrThrow().languageTag)
    }

    @Test
    fun `test that the fallback language is not requested when the page declares one`() = runTest {
        var fallbackRequested = false
        val provider =
            ContentExtractor.fromPage(
                pageContentExtractor = { Result.success("Article text") },
                pageMetadataExtractor = { Result.success(PageMetadata(languageTag = "de")) },
                fallbackLanguageTag = {
                    fallbackRequested = true
                    "en-US"
                },
            )

        assertEquals("de", provider.getContent("tab-1").getOrThrow().languageTag)
        assertFalse(fallbackRequested)
    }

    @Test
    fun `test that the fallback language is used when the page declares none`() = runTest {
        val provider =
            ContentExtractor.fromPage(
                pageContentExtractor = { Result.success("Article text") },
                pageMetadataExtractor = { Result.success(PageMetadata(languageTag = "")) },
                fallbackLanguageTag = { "en-US" },
            )

        val content = provider.getContent("tab-1").getOrThrow()

        assertEquals("en-US", content.languageTag)
        assertEquals(LanguageSource.Fallback, content.languageSource)
    }

    @Test
    fun `test that the fallback language is used when the page declares no usable language`() = runTest {
        listOf("-en", "e", "  ").forEach { tag ->
            val provider =
                ContentExtractor.fromPage(
                    pageContentExtractor = { Result.success("Article text") },
                    pageMetadataExtractor = { Result.success(PageMetadata(languageTag = tag)) },
                    fallbackLanguageTag = { "en-US" },
                )

            val content = provider.getContent("tab-1").getOrThrow()

            assertEquals("en-US", content.languageTag)
            assertEquals(LanguageSource.Fallback, content.languageSource)
        }
    }

    @Test
    fun `test that fromPage fails when the text cannot be extracted`() = runTest {
        val error = RuntimeException("Content failed")
        val provider =
            ContentExtractor.fromPage(
                pageContentExtractor = { Result.failure(error) },
                pageMetadataExtractor = { Result.success(PageMetadata(languageTag = "en")) },
                fallbackLanguageTag = { "en" },
            )

        val result = provider.getContent("tab-1")

        assertTrue(result.isFailure)
        assertEquals(error, result.exceptionOrNull())
    }

    @Test
    fun `test that fromPage fails when the metadata cannot be extracted`() = runTest {
        val error = RuntimeException("Metadata failed")
        val provider =
            ContentExtractor.fromPage(
                pageContentExtractor = { Result.success("Article text") },
                pageMetadataExtractor = { Result.failure(error) },
                fallbackLanguageTag = { "en" },
            )

        val result = provider.getContent("tab-1")

        assertTrue(result.isFailure)
        assertEquals(error, result.exceptionOrNull())
    }

    @Test
    fun `test that the text is not extracted when the metadata fails`() = runTest {
        var contentRequested = false
        val provider =
            ContentExtractor.fromPage(
                pageContentExtractor = {
                    contentRequested = true
                    Result.success("Article text")
                },
                pageMetadataExtractor = { Result.failure(RuntimeException("Metadata failed")) },
                fallbackLanguageTag = { "en" },
            )

        provider.getContent("tab-1")

        assertEquals(false, contentRequested)
    }
}
