/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.storage

import android.util.AtomicFile
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.io.StringWriter
import javax.xml.parsers.DocumentBuilderFactory
import javax.xml.transform.OutputKeys
import javax.xml.transform.TransformerConfigurationException
import javax.xml.transform.TransformerException
import javax.xml.transform.TransformerFactory
import javax.xml.transform.dom.DOMSource
import javax.xml.transform.stream.StreamResult
import kotlin.test.assertNotNull
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.spy
import org.w3c.dom.DOMException
import org.w3c.dom.DOMException.INVALID_CHARACTER_ERR
import org.w3c.dom.Document

@RunWith(AndroidJUnit4::class)
class SearchEngineWriterTest {
    @Test
    fun `buildSearchEngineXML builds search engine xml correctly`() {
        val writer = SearchEngineWriter()
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                inputEncoding = "UTF-8",
                resultUrls = listOf("https://www.example.com/search?q={searchTerms}'"),
            )

        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument()
        writer.buildSearchEngineXML(searchEngine, document)
        val searchXML = document.xmlToString()
        assertTrue(searchXML!!.contains(searchEngine.name))
        assertTrue(searchXML.contains(IMAGE_URI_PREFIX))
        assertTrue(searchXML.contains(URL_TYPE_SEARCH_HTML))
        assertTrue(searchXML.contains("https://www.example.com/search?q={searchTerms}"))
        assertTrue(searchXML.contains("UTF-8"))
        assertFalse(searchXML.contains(URL_TYPE_SUGGEST_JSON))
    }

    @Test
    fun `buildSearchEngineXML builds multiple resultUrls correctly`() {
        val writer = SearchEngineWriter()
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                resultUrls =
                    listOf(
                        "https://www.example.com/search?q=%s",
                        "https://www.example.com/search1?q=%s",
                        "https://www.example.com/search2?q=%s",
                    ),
            )

        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument()
        writer.buildSearchEngineXML(searchEngine, document)
        val searchXML = document.xmlToString()
        assertTrue(searchXML!!.contains(searchEngine.name))
        assertTrue(searchXML.contains(IMAGE_URI_PREFIX))
        assertTrue(searchXML.contains(URL_TYPE_SEARCH_HTML))
        searchEngine.resultUrls.forEach { assertTrue(searchXML.contains(it)) }
        assertFalse(searchXML.contains(URL_TYPE_SUGGEST_JSON))
    }

    @Test
    fun `buildSearchEngineXML builds suggestUrl correctly`() {
        val writer = SearchEngineWriter()
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                suggestUrl = "https://www.example.com/search-suggestion?q=%s",
            )

        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument()
        writer.buildSearchEngineXML(searchEngine, document)
        val searchXML = document.xmlToString()
        assertTrue(searchXML!!.contains(searchEngine.name))
        assertTrue(searchXML.contains(IMAGE_URI_PREFIX))
        assertFalse(searchXML.contains(URL_TYPE_SEARCH_HTML))
        assertTrue(searchXML.contains(URL_TYPE_SUGGEST_JSON))
        assertTrue(searchXML.contains("https://www.example.com/search-suggestion?q={searchTerms}"))
    }

    @Test
    fun `GIVEN a search engine with a trending URL WHEN building the search engine XML THEN the trendingUrl is built correctly`() {
        val writer = SearchEngineWriter()
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                trendingUrl = "https://www.example.com/search-suggestion?q=%s",
            )

        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument()
        writer.buildSearchEngineXML(searchEngine, document)
        val searchXML = document.xmlToString()
        assertTrue(searchXML!!.contains(searchEngine.name))
        assertTrue(searchXML.contains(IMAGE_URI_PREFIX))
        assertFalse(searchXML.contains(URL_TYPE_SEARCH_HTML))
        assertTrue(searchXML.contains(URL_TYPE_TRENDING_JSON))
        assertTrue(searchXML.contains("https://www.example.com/search-suggestion?q={searchTerms}"))
    }

    @Test
    fun `buildSearchEngineXML successfully for search engines with XML escaped characters`() {
        val writer = SearchEngineWriter()
        val invalidSearchEngineNameAmp =
            SearchEngine(
                id = "id1",
                name = "&&&example&&&",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                suggestUrl = "https://www.example.com/search-suggestion?q=%s",
            )
        val invalidResultUrlLessSign =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                resultUrls = listOf("https://www.example.com/search?<q=%s"),
            )
        val invalidResultUrlGreaterSign =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                resultUrls = listOf("https://www.example.com/search?>q=%s"),
            )
        val invalidSuggestionUrlApo =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                suggestUrl = "https://www.example.com/search-'suggestion'?q=%s",
            )

        assertNotNull(
            writer.buildSearchEngineXML(
                invalidSearchEngineNameAmp,
                DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument(),
            )
        )
        assertNotNull(
            writer.buildSearchEngineXML(
                invalidResultUrlLessSign,
                DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument(),
            )
        )
        assertNotNull(
            writer.buildSearchEngineXML(
                invalidResultUrlGreaterSign,
                DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument(),
            )
        )
        assertNotNull(
            writer.buildSearchEngineXML(
                invalidSuggestionUrlApo,
                DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument(),
            )
        )
    }

    @Test
    fun `saveSearchEngineXML returns false when failed to write to a bad file data`() {
        val writer = spy(SearchEngineWriter())
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                resultUrls = listOf("https://www.example.com/search?q=%s'"),
            )

        val badFile = AtomicFile(File("", ""))
        assertFalse(writer.saveSearchEngineXML(searchEngine, badFile))
    }

    @Test
    fun `saveSearchEngineXML returns false when there's DOMException while generating XML doc`() {
        val storage = CustomSearchEngineStorage(testContext)
        val writer = spy(SearchEngineWriter())
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                resultUrls = listOf("https://www.example.com/search?q=%s'"),
            )

        val file = storage.getSearchFile(searchEngine.id)
        val mockDoc: Document = mock()
        whenever(mockDoc.createElement(any())).thenThrow(DOMException(INVALID_CHARACTER_ERR, ""))
        assertFalse(writer.saveSearchEngineXML(searchEngine, file, mockDoc))
    }
}

private fun Document.xmlToString(): String? {
    val writer = StringWriter()
    try {
        val tf = TransformerFactory.newInstance().newTransformer()
        tf.setOutputProperty(OutputKeys.ENCODING, "UTF-8")
        tf.transform(DOMSource(this), StreamResult(writer))
    } catch (e: TransformerConfigurationException) {
        return null
    } catch (e: TransformerException) {
        return null
    }

    return writer.toString()
}
