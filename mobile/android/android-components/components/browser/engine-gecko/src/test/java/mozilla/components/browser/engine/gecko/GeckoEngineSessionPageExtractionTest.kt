/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.os.Looper.getMainLooper
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.concept.engine.pageextraction.PageExtractionError
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.PageExtractionController.PageExtractionException
import org.mozilla.geckoview.PageExtractionController.PageExtractionException.ERROR_MALFORMED_RESULT
import org.mozilla.geckoview.PageExtractionController.PageExtractionException.ERROR_NULL_RESULT
import org.mozilla.geckoview.PageExtractionController.PageExtractionException.ERROR_UNKNOWN
import org.mozilla.geckoview.PageExtractionController.PageMetadata
import org.mozilla.geckoview.PageExtractionController.SessionPageExtractor
import org.robolectric.Shadows.shadowOf

/**
 * Test cases for the "Page Extraction" feature of [GeckoEngineSession]
 */
@RunWith(AndroidJUnit4::class)
class GeckoEngineSessionPageExtractionTest {

    private lateinit var mockedSessionPageExtractor: SessionPageExtractor
    private lateinit var engineSession: GeckoEngineSession

    @Before
    fun setUp() {
        // set up Gecko session mocks
        mockedSessionPageExtractor = mock()
        val mockedGeckoSession: GeckoSession = mock()
        whenever(mockedGeckoSession.sessionPageExtractor).thenReturn(mockedSessionPageExtractor)

        engineSession = GeckoEngineSession(
            runtime = mock(),
            geckoSessionProvider = { mockedGeckoSession },
        )
    }

    @Test
    fun `given page extractor returns successfully but null result, then an unexpected null error is returned`() {
        // given that page extractor returns null content
        whenever(mockedSessionPageExtractor.getPageContent(any()))
            .thenReturn(GeckoResult.fromValue(null))

        // when we attempt to get page content
        var resultError: Throwable? = null
        engineSession.getPageContent(
            onResult = { },
            onException = { resultError = it },
        )

        // run pending tasks
        shadowOf(getMainLooper()).idle()

        // then assert that an unexpected null exception is received
        assertTrue(resultError is PageExtractionError.UnexpectedNull)
    }

    @Test
    fun `given page extractor returns a null content exception, then an unexpected null error is returned`() {
        // given that page extractor returns a null result exception
        whenever(mockedSessionPageExtractor.getPageContent(any()))
            .thenReturn(GeckoResult.fromException(PageExtractionException(ERROR_NULL_RESULT)))

        // when we attempt to get page content
        var resultError: Throwable? = null
        engineSession.getPageContent(
            onResult = { },
            onException = { resultError = it },
        )

        // run pending tasks
        shadowOf(getMainLooper()).idle()

        // then assert that an unexpected null exception is received
        assertTrue(resultError is PageExtractionError.UnexpectedNull)
    }

    @Test
    fun `given page extractor returns a malformed content exception, then a malformed content error is returned`() {
        // given that page extractor returns a malformed result exception
        whenever(mockedSessionPageExtractor.getPageContent(any()))
            .thenReturn(GeckoResult.fromException(PageExtractionException(ERROR_MALFORMED_RESULT)))

        // when we attempt to get page content
        var resultError: Throwable? = null
        engineSession.getPageContent(
            onResult = { },
            onException = { resultError = it },
        )

        // run pending tasks
        shadowOf(getMainLooper()).idle()

        // then assert that a malformed result error is received
        assertTrue(resultError is PageExtractionError.MalformedResult)
    }

    @Test
    fun `given page extractor returns an unknown exception, then an unknown error is returned`() {
        // given that page extractor returns an unknown exception
        whenever(mockedSessionPageExtractor.getPageContent(any()))
            .thenReturn(GeckoResult.fromException(PageExtractionException(ERROR_UNKNOWN)))

        // when we attempt to get page content
        var resultError: Throwable? = null
        engineSession.getPageContent(
            onResult = { },
            onException = { resultError = it },
        )

        // run pending tasks
        shadowOf(getMainLooper()).idle()

        // then assert that an unknown error is received
        assertTrue(resultError is PageExtractionError.UnknownError)
    }

    @Test
    fun `given page extractor returns content successfully, then a the result is returned without error`() {
        // given that page extractor returns content
        whenever(mockedSessionPageExtractor.getPageContent(any()))
            .thenReturn(GeckoResult.fromValue("mozilla.org awesome blog"))

        // when we attempt to get page content
        var resultContent: String = ""
        var resultError: Throwable? = null
        engineSession.getPageContent(
            onResult = { resultContent = it },
            onException = { resultError = it },
        )

        // run pending tasks
        shadowOf(getMainLooper()).idle()

        // then assert that content is returned without error
        assertEquals("mozilla.org awesome blog", resultContent)
        assertNull("Expected no error to be returned. Got $resultError instead", resultError)
    }

    @Test
    fun `given page metadata extractor returns successfully but null result, then an unexpected null error is returned`() {
        whenever(mockedSessionPageExtractor.pageMetadata)
            .thenReturn(GeckoResult.fromValue(null))

        var resultError: Throwable? = null
        engineSession.getPageMetadata(
            onResult = { },
            onException = { resultError = it },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(resultError is PageExtractionError.UnexpectedNull)
    }

    @Test
    fun `given page metadata extractor returns a null result exception, then an unexpected null error is returned`() {
        whenever(mockedSessionPageExtractor.pageMetadata)
            .thenReturn(GeckoResult.fromException(PageExtractionException(ERROR_NULL_RESULT)))

        var resultError: Throwable? = null
        engineSession.getPageMetadata(
            onResult = { },
            onException = { resultError = it },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(resultError is PageExtractionError.UnexpectedNull)
    }

    @Test
    fun `given page metadata extractor returns a malformed result exception, then a malformed result error is returned`() {
        whenever(mockedSessionPageExtractor.pageMetadata)
            .thenReturn(GeckoResult.fromException(PageExtractionException(ERROR_MALFORMED_RESULT)))

        var resultError: Throwable? = null
        engineSession.getPageMetadata(
            onResult = { },
            onException = { resultError = it },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(resultError is PageExtractionError.MalformedResult)
    }

    @Test
    fun `given page metadata extractor returns an unknown exception, then an unknown error is returned`() {
        whenever(mockedSessionPageExtractor.pageMetadata)
            .thenReturn(GeckoResult.fromException(PageExtractionException(ERROR_UNKNOWN)))

        var resultError: Throwable? = null
        engineSession.getPageMetadata(
            onResult = { },
            onException = { resultError = it },
        )

        shadowOf(getMainLooper()).idle()

        assertTrue(resultError is PageExtractionError.UnknownError)
    }

    @Test
    fun `given page metadata extractor returns metadata successfully, then the result is returned without error`() {
        whenever(mockedSessionPageExtractor.pageMetadata)
            .thenReturn(GeckoResult.fromValue(PageMetadata(arrayOf("Article"), 42, "en")))

        var resultMetadata: mozilla.components.concept.engine.pageextraction.PageMetadata? = null
        var resultError: Throwable? = null
        engineSession.getPageMetadata(
            onResult = { resultMetadata = it },
            onException = { resultError = it },
        )

        shadowOf(getMainLooper()).idle()

        assertNull("Expected no error to be returned. Got $resultError instead", resultError)
        assertEquals(listOf("Article"), resultMetadata?.structuredDataTypes)
        assertEquals(42, resultMetadata?.wordCount)
        assertEquals("en", resultMetadata?.language)
    }
}
