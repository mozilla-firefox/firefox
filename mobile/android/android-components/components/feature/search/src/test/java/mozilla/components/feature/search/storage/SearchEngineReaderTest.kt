/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.storage

import android.graphics.Bitmap
import android.util.AtomicFile
import android.util.Base64
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.io.IOException
import java.util.Locale
import kotlin.test.assertNotNull
import mozilla.appservices.remotesettings.RemoteSettingsClient
import mozilla.appservices.search.SearchEngineClassification
import mozilla.appservices.search.SearchEngineClassificationV3
import mozilla.appservices.search.SearchEngineDefinition
import mozilla.appservices.search.SearchEngineDefinitionV3
import mozilla.appservices.search.SearchEnginePartnerDetails
import mozilla.appservices.search.SearchEngineUrl
import mozilla.appservices.search.SearchEngineUrls
import mozilla.appservices.search.SearchUrlParam
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.feature.search.icons.AttachmentModel
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class SearchEngineReaderTest {

    private lateinit var mockClient: RemoteSettingsClient
    private lateinit var emptyByteArray: ByteArray
    private val validMimeType = "image/png"
    private val invalidMimeType = "image/gif"

    @Before
    fun setUp() {
        mockClient = mock<RemoteSettingsClient>()
        emptyByteArray = ByteArray(0)
    }

    @Test
    fun `SearchEngineReader can read from a file`() {
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                inputEncoding = "ISO-8859-1",
                type = SearchEngine.Type.CUSTOM,
                resultUrls = listOf("https://www.example.com/search"),
            )
        val readSearchEngine = saveAndLoadSearchEngine(searchEngine)

        assertEquals(searchEngine.id, readSearchEngine.id)
        assertEquals(searchEngine.name, readSearchEngine.name)
        assertEquals(searchEngine.inputEncoding, readSearchEngine.inputEncoding)
        assertEquals(searchEngine.type, readSearchEngine.type)
        assertEquals(searchEngine.resultUrls, readSearchEngine.resultUrls)
        assertTrue(readSearchEngine.isGeneral)
    }

    @Test(expected = IOException::class)
    fun `Parsing not existing file will throw exception`() {
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                resultUrls = listOf("https://www.example.com/search"),
            )
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.CUSTOM)
        val invalidFile = AtomicFile(File("", ""))
        reader.loadFile(searchEngine.id, invalidFile)
    }

    @Test
    fun `WHEN SearchEngineReader is loading bundled search engines from a file THEN the correct SearchEngine properties are parsed`() {
        for (id in GENERAL_SEARCH_ENGINE_IDS + setOf("mozilla", "wikipedia")) {
            val searchEngine =
                SearchEngine(
                    id = id,
                    name = "example",
                    icon = mock(),
                    type = SearchEngine.Type.BUNDLED,
                    resultUrls = listOf("https://www.example.com/search"),
                )
            val readSearchEngine = saveAndLoadSearchEngine(searchEngine)

            assertEquals(searchEngine.id, readSearchEngine.id)
            assertEquals(searchEngine.name, readSearchEngine.name)
            assertEquals(searchEngine.type, readSearchEngine.type)
            assertEquals(searchEngine.resultUrls, readSearchEngine.resultUrls)
            assertEquals(id in GENERAL_SEARCH_ENGINE_IDS, readSearchEngine.isGeneral)
        }
    }

    @Test
    fun `GIVEN a search engine with a trending URL WHEN SearchEngineReader loads the search engine from a file THEN the trending URL is correctly parsed`() {
        val searchEngine =
            SearchEngine(
                id = "id1",
                name = "example",
                icon = mock(),
                type = SearchEngine.Type.CUSTOM,
                trendingUrl = "https://www.example.com/complete/search?client=firefox&channel=ftr&q={searchTerms}",
            )
        val readSearchEngine = saveAndLoadSearchEngine(searchEngine)

        assertEquals(searchEngine.id, readSearchEngine.id)
        assertEquals(searchEngine.name, readSearchEngine.name)
        assertEquals(searchEngine.type, readSearchEngine.type)
        assertEquals(searchEngine.trendingUrl, readSearchEngine.trendingUrl)
    }

    private fun saveAndLoadSearchEngine(searchEngine: SearchEngine): SearchEngine {
        val storage = CustomSearchEngineStorage(testContext)
        val writer = SearchEngineWriter()
        val reader = SearchEngineReader(context = testContext, type = searchEngine.type)
        val file = storage.getSearchFile(searchEngine.id)

        writer.saveSearchEngineXML(searchEngine, file)

        return reader.loadFile(searchEngine.id, file)
    }

    @Test
    fun `GIVEN {partnerCode} in value of a SearchURLParam THEN it is replaced by actual partnerCode`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "client",
                value = "{partnerCode}",
                enterpriseValue = null,
                experimentConfig = null,
            )
        searchEngineDefinition.partnerCode = "test-firefox-code"
        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        assertEquals("https://www.google.com/search?client=test-firefox-code", searchEngine.resultUrls[0])
    }

    @Test
    fun `Given null value of a SearchURLParam THEN it is not appended to the URL`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "channel",
                value = null,
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        assertEquals("https://www.google.com/search", searchEngine.resultUrls[0])
    }

    @Test
    fun `GIVEN searchTermParamName in SearchEngineUrl THEN add a new param with name searchTermParamName and value {searchTerms}`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.urls.search.searchTermParamName = "test"

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        assertEquals("https://www.google.com/search?test=%7BsearchTerms%7D", searchEngine.resultUrls[0])
    }

    @Test
    fun `GIVEN searchTermParamName in SearchEngineUrl and {searchTerms} in base url THEN don't add a new param with value {searchTerms}`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.urls.search.searchTermParamName = "test"
        searchEngineDefinition.urls.search.base = "https://www.google.com/q={searchTerms}"

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        assertEquals("https://www.google.com/q={searchTerms}", searchEngine.resultUrls[0])
    }

    @Test
    fun `GIVEN search, suggest and trending URLs THEN they are correctly parsed`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.urls.search.base = "https://www.google.com/search"
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "search-test-name",
                value = "search-test-value",
                enterpriseValue = null,
                experimentConfig = null,
            )
        searchEngineDefinition.urls.search.searchTermParamName = "test"

        searchEngineDefinition.urls.suggestions =
            SearchEngineUrl(
                base = "https://www.google.com/suggest/search",
                method = "GET",
                params =
                    listOf(
                        SearchUrlParam(
                            name = "suggestions-test-name",
                            value = "suggestions-test-value",
                            enterpriseValue = null,
                            experimentConfig = null,
                        )
                    ),
                searchTermParamName = "test2",
                displayName = null,
            )

        searchEngineDefinition.urls.trending =
            SearchEngineUrl(
                base = "https://www.google.com/trending/search",
                method = "GET",
                params =
                    listOf(
                        SearchUrlParam(
                            name = "trending-test-name",
                            value = "trending-test-value",
                            enterpriseValue = null,
                            experimentConfig = null,
                        )
                    ),
                searchTermParamName = "test3",
                displayName = null,
            )

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals(searchEngineDefinition.identifier, searchEngine.id)
        assertEquals(searchEngineDefinition.name, searchEngine.name)
        assertEquals(searchEngineDefinition.charset, searchEngine.inputEncoding)

        assertEquals(
            "https://www.google.com/search?search-test-name=search-test-value&test=%7BsearchTerms%7D",
            searchEngine.resultUrls[0],
        )
        assertEquals(
            "https://www.google.com/suggest/search?suggestions-test-name=suggestions-test-value&test2=%7BsearchTerms%7D",
            searchEngine.suggestUrl,
        )
        assertEquals(
            "https://www.google.com/trending/search?trending-test-name=trending-test-value&test3=%7BsearchTerms%7D",
            searchEngine.trendingUrl,
        )
    }

    @Test
    fun `GIVEN null name THEN throw exception`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.name = ""

        val exception =
            assertThrows(IllegalArgumentException::class.java) {
                reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, "", mock())
            }
        assertEquals("Search engine name cannot be empty", exception.message)
    }

    @Test
    fun `GIVEN null charset THEN throw exception`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.charset = ""

        val exception =
            assertThrows(IllegalArgumentException::class.java) {
                reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, "", mock())
            }
        assertEquals("Search engine charset cannot be empty", exception.message)
    }

    @Test
    fun `GIVEN null identifier THEN throw exception`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.identifier = ""

        val exception =
            assertThrows(IllegalArgumentException::class.java) {
                reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, "", mock())
            }
        assertEquals("Search engine identifier cannot be empty", exception.message)
    }

    @Test
    fun `GIVEN valid jpeg image THEN readImageAPI decodes it`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        assertNotNull(searchEngine.icon)
    }

    @Test
    fun `GIVEN valid png image THEN readImageAPI decodes it`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        assertNotNull(searchEngine.icon)
    }

    @Test
    fun `GIVEN valid ico image THEN readImageAPI decodes it`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        assertNotNull(searchEngine.icon)
    }

    @Test
    fun `GIVEN a png larger than the icon size THEN readImageAPI still decodes it rather than falling back`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        val defaultIcon = Bitmap.createBitmap(7, 7, Bitmap.Config.ARGB_8888)
        val icon = Base64.decode(LARGE_PNG_BASE64, Base64.DEFAULT)

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, icon, validMimeType, defaultIcon)

        assertNotSame(defaultIcon, searchEngine.icon)
        assertEquals(LARGE_PNG_SIZE, searchEngine.icon.width)
        assertEquals(LARGE_PNG_SIZE, searchEngine.icon.height)
    }

    @Test
    fun `GIVEN invalid image mimetype THEN readImageAPI returns defaultIcon`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        val defaultIcon = Bitmap.createBitmap(100, 100, Bitmap.Config.ARGB_8888)
        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, invalidMimeType, defaultIcon)
        assertEquals(defaultIcon, searchEngine.icon)
    }

    @Test
    fun `GIVEN invalid image location THEN readImageAPI returns defaultIcon`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        val attachmentModel =
            AttachmentModel(
                filename = "test",
                mimetype = "image/png",
                location = "test",
                hash = "test",
                size = 100u,
            )

        val defaultIcon = Bitmap.createBitmap(100, 100, Bitmap.Config.ARGB_8888)
        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, null, attachmentModel.mimetype, defaultIcon)
        assertEquals(defaultIcon, searchEngine.icon)
    }

    @Test
    fun `GIVEN specific icons url prefix THEN readImageAPI reads from correct url`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        val attachmentModel =
            AttachmentModel(
                filename = "test",
                mimetype = "image/x-icon",
                location = "main-workspace/search-config-icons/53f837f7-abf4-463d-b8a7-d4526864a7de.ico",
                hash = "test",
                size = 100u,
            )

        val defaultIcon = Bitmap.createBitmap(100, 100, Bitmap.Config.ARGB_8888)
        val dummyIcoBytes = ByteArray(8)
        val searchEngine =
            reader.loadStreamAPI(searchEngineDefinition, dummyIcoBytes, attachmentModel.mimetype, defaultIcon)
        assertEquals(defaultIcon, searchEngine.icon)
    }

    @Test
    fun `GIVEN {acceptLanguages} in params THEN it is replaced by current locale`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "lang",
                value = "{acceptLanguages}",
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())
        val expected =
            LocaleManager.getCurrentLocale(testContext)?.toLanguageTag() ?: Locale.getDefault().toLanguageTag()

        assertEquals("https://www.google.com/search?lang=$expected", searchEngine.resultUrls[0])
    }

    @Test
    fun `GIVEN {acceptLanguages} and {partnerCode} in trending params with context THEN both are replaced`() {
        val reader =
            SearchEngineReader(
                context = testContext,
                type = SearchEngine.Type.BUNDLED,
            )
        val searchEngineDefinition = sampleSearchEngineDefinitionData()

        // Override partner code so we can assert it explicitly
        searchEngineDefinition.partnerCode = "test-firefox-code"

        searchEngineDefinition.urls.trending =
            SearchEngineUrl(
                base = "https://www.google.com/trending/search",
                method = "GET",
                params =
                    listOf(
                        SearchUrlParam(
                            name = "lang",
                            value = "{acceptLanguages}",
                            enterpriseValue = null,
                            experimentConfig = null,
                        ),
                        SearchUrlParam(
                            name = "client",
                            value = "{partnerCode}",
                            enterpriseValue = null,
                            experimentConfig = null,
                        ),
                    ),
                searchTermParamName = null,
                displayName = null,
            )

        val searchEngine =
            reader.loadStreamAPI(
                searchEngineDefinition,
                emptyByteArray,
                validMimeType,
                mock(),
            )

        val expectedLanguage =
            LocaleManager.getCurrentLocale(testContext)?.toLanguageTag() ?: Locale.getDefault().toLanguageTag()

        assertEquals(
            "https://www.google.com/trending/search?lang=$expectedLanguage&client=test-firefox-code",
            searchEngine.trendingUrl,
        )
    }

    @Test
    fun `GIVEN an empty partnerCode in a v2 definition THEN the parameter is still appended empty`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionData()
        searchEngineDefinition.partnerCode = ""
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "client",
                value = "{partnerCode}",
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPI(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals("https://www.google.com/search?client=", searchEngine.resultUrls[0])
    }

    @Test
    fun `GIVEN a v3 definition with default partner details THEN the partner code and telemetry suffix are used`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "client",
                value = "{partnerCode}",
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals("https://www.google.com/search?client=firefox-b-m", searchEngine.resultUrls[0])
        assertEquals("b-m", searchEngine.telemetrySuffix)
    }

    @Test
    fun `GIVEN a v3 definition with several access points THEN only the default one is read`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.partner =
            mapOf(
                DEFAULT_SEARCH_ACCESS_POINT to
                    SearchEnginePartnerDetails(code = "firefox-b-m", telemetrySuffix = "b-m"),
                "newtab" to SearchEnginePartnerDetails(code = "firefox-b-n", telemetrySuffix = "b-n"),
            )
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "client",
                value = "{partnerCode}",
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals("https://www.google.com/search?client=firefox-b-m", searchEngine.resultUrls[0])
        assertEquals("b-m", searchEngine.telemetrySuffix)
    }

    @Test
    fun `GIVEN a v3 definition with no partner details THEN the partner code is omitted and there is no suffix`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.partner = null
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "client",
                value = "{partnerCode}",
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals("https://www.google.com/search", searchEngine.resultUrls[0])
        assertNull(searchEngine.telemetrySuffix)
    }

    @Test
    fun `GIVEN a v3 definition with no default access point THEN the partner code is omitted and there is no suffix`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.partner =
            mapOf("newtab" to SearchEnginePartnerDetails(code = "firefox-b-n", telemetrySuffix = "b-n"))
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "client",
                value = "{partnerCode}",
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals("https://www.google.com/search", searchEngine.resultUrls[0])
        assertNull(searchEngine.telemetrySuffix)
    }

    @Test
    fun `GIVEN a v3 definition with an empty partner code THEN the code is omitted but the suffix still applies`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.partner =
            mapOf(DEFAULT_SEARCH_ACCESS_POINT to SearchEnginePartnerDetails(code = "", telemetrySuffix = "b-m"))
        searchEngineDefinition.urls.search.params +=
            SearchUrlParam(
                name = "client",
                value = "{partnerCode}",
                enterpriseValue = null,
                experimentConfig = null,
            )

        val searchEngine = reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals("https://www.google.com/search", searchEngine.resultUrls[0])
        assertEquals("b-m", searchEngine.telemetrySuffix)
    }

    @Test
    fun `GIVEN a v3 definition with suggest and trending URLs THEN they are read with the default partner code`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.urls.suggestions =
            SearchEngineUrl(
                base = "https://www.google.com/complete/search",
                method = "GET",
                params =
                    listOf(
                        SearchUrlParam(
                            name = "client",
                            value = "{partnerCode}",
                            enterpriseValue = null,
                            experimentConfig = null,
                        )
                    ),
                searchTermParamName = "q",
                displayName = null,
            )
        searchEngineDefinition.urls.trending =
            SearchEngineUrl(
                base = "https://www.google.com/trending/search",
                method = "GET",
                params =
                    listOf(
                        SearchUrlParam(
                            name = "client",
                            value = "{partnerCode}",
                            enterpriseValue = null,
                            experimentConfig = null,
                        )
                    ),
                searchTermParamName = null,
                displayName = null,
            )

        val searchEngine = reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals(
            "https://www.google.com/complete/search?client=firefox-b-m&q=%7BsearchTerms%7D",
            searchEngine.suggestUrl,
        )
        assertEquals("https://www.google.com/trending/search?client=firefox-b-m", searchEngine.trendingUrl)
    }

    @Test
    fun `GIVEN a v3 definition with an empty name THEN throw exception`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.name = ""

        val exception =
            assertThrows(IllegalArgumentException::class.java) {
                reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, "", mock())
            }
        assertEquals("Search engine name cannot be empty", exception.message)
    }

    @Test
    fun `GIVEN a v3 definition with an empty charset THEN throw exception`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.charset = ""

        val exception =
            assertThrows(IllegalArgumentException::class.java) {
                reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, "", mock())
            }
        assertEquals("Search engine charset cannot be empty", exception.message)
    }

    @Test
    fun `GIVEN a v3 definition with an empty identifier THEN throw exception`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.identifier = ""

        val exception =
            assertThrows(IllegalArgumentException::class.java) {
                reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, "", mock())
            }
        assertEquals("Search engine identifier cannot be empty", exception.message)
    }

    @Test
    fun `GIVEN a v3 definition THEN the name, charset and optional flag are read`() {
        val reader = SearchEngineReader(context = testContext, type = SearchEngine.Type.BUNDLED)
        val searchEngineDefinition = sampleSearchEngineDefinitionDataV3()
        searchEngineDefinition.optional = true

        val searchEngine = reader.loadStreamAPIV3(searchEngineDefinition, emptyByteArray, validMimeType, mock())

        assertEquals("google", searchEngine.id)
        assertEquals("Google", searchEngine.name)
        assertEquals("UTF-8", searchEngine.inputEncoding)
        assertTrue(searchEngine.isOptional)
    }

    private fun sampleSearchEngineDefinitionDataV3(): SearchEngineDefinitionV3 {
        return SearchEngineDefinitionV3(
            aliases = listOf("google"),
            charset = "UTF-8",
            classification = SearchEngineClassificationV3.GENERAL,
            identifier = "google",
            isNewUntil = null,
            name = "Google",
            optional = false,
            partner =
                mapOf(
                    DEFAULT_SEARCH_ACCESS_POINT to
                        SearchEnginePartnerDetails(code = "firefox-b-m", telemetrySuffix = "b-m")
                ),
            urls =
                SearchEngineUrls(
                    search =
                        SearchEngineUrl(
                            base = "https://www.google.com/search",
                            method = "GET",
                            params = emptyList(),
                            searchTermParamName = null,
                            displayName = null,
                        ),
                    suggestions = null,
                    trending = null,
                    searchForm = null,
                    visualSearch = null,
                ),
            orderHint = null,
            deprecated = false,
        )
    }

    private fun sampleSearchEngineDefinitionData(): SearchEngineDefinition {
        val engineDefinition =
            SearchEngineDefinition(
                aliases = listOf("google"),
                charset = "UTF-8",
                classification = SearchEngineClassification.GENERAL,
                identifier = "google",
                isNewUntil = null,
                name = "Google",
                optional = false,
                partnerCode = "firefox-b-m",
                telemetrySuffix = "b-m",
                urls =
                    SearchEngineUrls(
                        search =
                            SearchEngineUrl(
                                base = "https://www.google.com/search",
                                method = "GET",
                                params = emptyList(),
                                searchTermParamName = null,
                                displayName = null,
                            ),
                        suggestions = null,
                        trending = null,
                        searchForm = null,
                        visualSearch = null,
                    ),
                orderHint = null,
                clickUrl = null,
            )
        return engineDefinition
    }
}

private const val LARGE_PNG_SIZE = 128

// A 128x128 PNG, comfortably larger than the size a search engine icon is drawn at.
private const val LARGE_PNG_BASE64 =
    "iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAIAAABMXPacAAAAyElEQVR42u3RQQ0AAAjEsJOGNKQhDRnwaDIFa6pHh8UCAAAEAIAAABAAAAIAQAAACAAAAQAgAAAEAIAAABAAAAIAQAAACAAAAQAgAAAEAIAAABAAAAIAQAAACAAAAQAgAAAEAIAAABAAAAIAQAAACAAAAQAAwAUAAAQAgAAAEAAAAgBAAAAIAAABACAAAAQAgAAAEAAAAgBAAAAIAAABACAAAAQAgAAAEAAAAgBAAAAIAAABACAAAAQAgAAAEAAAAgBAAAAIwIcWRmIFoZah+J4AAAAASUVORK5CYII="
