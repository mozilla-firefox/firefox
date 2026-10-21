/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.storage

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.util.AtomicFile
import android.util.Base64
import androidx.core.net.toUri
import java.io.IOException
import java.io.InputStream
import java.io.InputStreamReader
import java.nio.charset.StandardCharsets
import mozilla.appservices.search.SearchEngineClassification
import mozilla.appservices.search.SearchEngineClassificationV3
import mozilla.appservices.search.SearchEngineDefinition
import mozilla.appservices.search.SearchEngineDefinitionV3
import mozilla.appservices.search.SearchEngineUrls
import mozilla.appservices.search.SearchUrlParam
import mozilla.components.browser.icons.decoder.ICOIconDecoder
import mozilla.components.browser.icons.decoder.SvgIconDecoder
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.feature.search.middleware.SearchExtraParams
import mozilla.components.support.images.DesiredSize
import mozilla.components.support.locale.LocaleManager
import org.xmlpull.v1.XmlPullParser
import org.xmlpull.v1.XmlPullParserException
import org.xmlpull.v1.XmlPullParserFactory

internal const val URL_TYPE_SUGGEST_JSON = "application/x-suggestions+json"
internal const val URL_TYPE_TRENDING_JSON = "application/x-trending+json"
internal const val URL_TYPE_SEARCH_HTML = "text/html"
internal const val URL_REL_MOBILE = "mobile"
internal const val IMAGE_URI_PREFIX = "data:image/png;base64,"
internal const val GOOGLE_ID = "google"
internal const val DEFAULT_SEARCH_ACCESS_POINT = "default"
private const val TARGET_SIZE = 32
private const val MAX_SIZE = 32

// List of general search engine ids, taken from
// https://searchfox.org/mozilla-central/rev/ef0aa879e94534ffd067a3748d034540a9fc10b0/toolkit/components/search/SearchUtils.sys.mjs#200
internal val GENERAL_SEARCH_ENGINE_IDS =
    setOf(
        GOOGLE_ID,
        "ddg",
        "bing",
        "baidu",
        "ecosia",
        "qwant",
        "yahoo-jp",
        "seznam-cz",
        "coccoc",
        "baidu",
    )

/**
 * A simple XML reader for search engine plugins.
 *
 * @param context the [Context] used to resolve the current locale dynamically. The application context will be stored
 *   to ensure locale changes made by the user are reflected in search URLs.
 * @param type the [SearchEngine.Type] that the read [SearchEngine]s will get assigned.
 * @param searchExtraParams Optional search extra params.
 */
internal class SearchEngineReader(
    context: Context,
    private val type: SearchEngine.Type,
    private val searchExtraParams: SearchExtraParams? = null,
) {
    private val applicationContext = context.applicationContext

    private class SearchEngineBuilder(
        private val type: SearchEngine.Type,
        private val identifier: String,
        private val telemetrySuffix: String? = null,
    ) {
        var resultsUrls: MutableList<String> = mutableListOf()
        var suggestUrl: String? = null
        var trendingUrl: String? = null
        var name: String? = null
        var icon: Bitmap? = null
        var inputEncoding: String? = null
        var isGeneral: Boolean = false
        var isOptional: Boolean = false

        fun toSearchEngine() =
            SearchEngine(
                id = identifier,
                name = name!!,
                icon = icon!!,
                type = type,
                resultUrls = resultsUrls,
                suggestUrl = suggestUrl,
                trendingUrl = trendingUrl,
                inputEncoding = inputEncoding,
                isGeneral = isGeneralSearchEngine(identifier, type), // Will be replaced with builder.isGeneral
                isOptional = isOptional,
                telemetrySuffix = telemetrySuffix,
            )

        /**
         * Returns true if the provided [type] is a custom search engine or the [identifier] is included in
         * [GENERAL_SEARCH_ENGINE_IDS].
         */
        private fun isGeneralSearchEngine(identifier: String, type: SearchEngine.Type): Boolean =
            type == SearchEngine.Type.CUSTOM ||
                identifier.startsWith(GOOGLE_ID) ||
                GENERAL_SEARCH_ENGINE_IDS.contains(identifier)
    }

    /** Loads [SearchEngine] from a provided [file] */
    fun loadFile(identifier: String, file: AtomicFile): SearchEngine {
        return loadStream(identifier, file.openRead())
    }

    /**
     * Loads a <code>SearchEngine</code> from the given <code>stream</code> and assigns it the given
     * <code>identifier</code>.
     */
    @Throws(IOException::class, XmlPullParserException::class)
    fun loadStream(identifier: String, stream: InputStream): SearchEngine {
        val builder = SearchEngineBuilder(type, identifier)

        val parser = XmlPullParserFactory.newInstance().newPullParser()
        parser.setInput(InputStreamReader(stream, StandardCharsets.UTF_8))
        parser.next()

        readSearchPlugin(parser, builder)

        return builder.toSearchEngine()
    }

    @Throws(XmlPullParserException::class, IOException::class)
    private fun readSearchPlugin(parser: XmlPullParser, builder: SearchEngineBuilder) {
        if (XmlPullParser.START_TAG != parser.eventType) {
            throw XmlPullParserException("Expected start tag: " + parser.positionDescription)
        }

        val name = parser.name
        if ("SearchPlugin" != name && "OpenSearchDescription" != name) {
            throw XmlPullParserException(
                "Expected <SearchPlugin> or <OpenSearchDescription> as root tag: ${parser.positionDescription}"
            )
        }

        while (parser.next() != XmlPullParser.END_TAG) {
            if (parser.eventType != XmlPullParser.START_TAG) {
                continue
            }

            when (parser.name) {
                "ShortName" -> readShortName(parser, builder)
                "Url" -> readUrl(parser, builder)
                "Image" -> readImage(parser, builder)
                "InputEncoding" -> readInputEncoding(parser, builder)
                else -> skip(parser)
            }
        }
    }

    @Throws(XmlPullParserException::class, IOException::class)
    private fun readUrl(parser: XmlPullParser, builder: SearchEngineBuilder) {
        parser.require(XmlPullParser.START_TAG, null, "Url")

        val type = parser.getAttributeValue(null, "type")
        val template = parser.getAttributeValue(null, "template")
        val rel = parser.getAttributeValue(null, "rel")

        val url = buildString {
            append(readUri(parser, template))
            searchExtraParams?.let {
                with(it) {
                    if (builder.name == searchEngineName) {
                        featureEnablerParam?.let { append("&$featureEnablerName=$it") }
                        append("&$channelIdName=$channelIdParam")
                    }
                }
            }
        }

        when (type) {
            URL_TYPE_SEARCH_HTML -> {
                // Prefer mobile URIs.
                if (rel != null && rel == URL_REL_MOBILE) {
                    builder.resultsUrls.add(0, url)
                } else {
                    builder.resultsUrls.add(url)
                }
            }
            URL_TYPE_SUGGEST_JSON -> builder.suggestUrl = url
            URL_TYPE_TRENDING_JSON -> builder.trendingUrl = url
        }
    }

    @Throws(XmlPullParserException::class, IOException::class)
    private fun readUri(parser: XmlPullParser, template: String): Uri {
        var uri = template.toUri()

        while (parser.next() != XmlPullParser.END_TAG) {
            if (parser.eventType != XmlPullParser.START_TAG) {
                continue
            }

            if (parser.name == "Param") {
                val name = parser.getAttributeValue(null, "name")
                val value = parser.getAttributeValue(null, "value")
                uri = uri.buildUpon().appendQueryParameter(name, value).build()
                parser.nextTag()
            } else {
                skip(parser)
            }
        }

        return uri
    }

    @Throws(XmlPullParserException::class, IOException::class)
    private fun skip(parser: XmlPullParser) {
        if (parser.eventType != XmlPullParser.START_TAG) {
            throw IllegalStateException()
        }
        var depth = 1
        while (depth != 0) {
            when (parser.next()) {
                XmlPullParser.END_TAG -> depth--
                XmlPullParser.START_TAG -> depth++
            // else: Do nothing - we're skipping content
            }
        }
    }

    @Throws(IOException::class, XmlPullParserException::class)
    private fun readShortName(parser: XmlPullParser, builder: SearchEngineBuilder) {
        parser.require(XmlPullParser.START_TAG, null, "ShortName")
        if (parser.next() == XmlPullParser.TEXT) {
            builder.name = parser.text
            parser.nextTag()
        }
    }

    @Throws(IOException::class, XmlPullParserException::class)
    private fun readImage(parser: XmlPullParser, builder: SearchEngineBuilder) {
        parser.require(XmlPullParser.START_TAG, null, "Image")

        if (parser.next() != XmlPullParser.TEXT) {
            return
        }

        val uri = parser.text
        if (!uri.startsWith(IMAGE_URI_PREFIX)) {
            return
        }

        val raw = Base64.decode(uri.substring(IMAGE_URI_PREFIX.length), Base64.DEFAULT)

        builder.icon = BitmapFactory.decodeByteArray(raw, 0, raw.size)

        parser.nextTag()
    }

    @Throws(IOException::class, XmlPullParserException::class)
    private fun readInputEncoding(parser: XmlPullParser, builder: SearchEngineBuilder) {
        parser.require(XmlPullParser.START_TAG, null, "InputEncoding")
        if (parser.next() == XmlPullParser.TEXT) {
            builder.inputEncoding = parser.text
            parser.nextTag()
        }
    }

    /**
     * Loads a <code>SearchEngine</code> from the given <code>stream</code> and assigns it the given
     * <code>identifier</code>.
     */
    @Throws(IllegalArgumentException::class)
    fun loadStreamAPI(
        engineDefinition: SearchEngineDefinition,
        attachmentModel: ByteArray?,
        mimetype: String,
        defaultIcon: Bitmap,
    ): SearchEngine {
        require(engineDefinition.name.isNotBlank()) { "Search engine name cannot be empty" }
        require(engineDefinition.charset.isNotBlank()) { "Search engine charset cannot be empty" }
        require(engineDefinition.identifier.isNotBlank()) { "Search engine identifier cannot be empty" }

        val builder =
            SearchEngineBuilder(
                type,
                engineDefinition.identifier,
                engineDefinition.telemetrySuffix,
            )
        builder.name = engineDefinition.name
        builder.inputEncoding = engineDefinition.charset
        builder.isGeneral = engineDefinition.classification == SearchEngineClassification.GENERAL
        builder.isOptional = engineDefinition.optional
        readUrlAPI(engineDefinition.urls, engineDefinition.partnerCode, builder)
        readImageAPI(attachmentModel, mimetype, builder, defaultIcon)

        return builder.toSearchEngine()
    }

    /**
     * Loads a <code>SearchEngine</code> from a search-config-v3 <code>engineDefinition</code>.
     *
     * The partner details of the [DEFAULT_SEARCH_ACCESS_POINT] access point supply the partner code and telemetry
     * suffix, which search-config-v2 defines once per engine.
     */
    @Throws(IllegalArgumentException::class)
    fun loadStreamAPIV3(
        engineDefinition: SearchEngineDefinitionV3,
        attachmentModel: ByteArray?,
        mimetype: String,
        defaultIcon: Bitmap,
    ): SearchEngine {
        require(engineDefinition.name.isNotBlank()) { "Search engine name cannot be empty" }
        require(engineDefinition.charset.isNotBlank()) { "Search engine charset cannot be empty" }
        require(engineDefinition.identifier.isNotBlank()) { "Search engine identifier cannot be empty" }

        val partner = engineDefinition.partner?.get(DEFAULT_SEARCH_ACCESS_POINT)
        val builder =
            SearchEngineBuilder(
                type,
                engineDefinition.identifier,
                partner?.telemetrySuffix,
            )
        builder.name = engineDefinition.name
        builder.inputEncoding = engineDefinition.charset
        builder.isGeneral = engineDefinition.classification == SearchEngineClassificationV3.GENERAL
        builder.isOptional = engineDefinition.optional
        readUrlAPI(engineDefinition.urls, partner?.code?.takeIf { it.isNotEmpty() }, builder)
        readImageAPI(attachmentModel, mimetype, builder, defaultIcon)

        return builder.toSearchEngine()
    }

    @Throws(IllegalArgumentException::class)
    private fun readUrlAPI(urls: SearchEngineUrls, partnerCode: String?, builder: SearchEngineBuilder) {
        requireNotNull(urls.search) { "Search engine URL cannot be empty" }
        builder.resultsUrls.add(
            buildUrlWithParams(
                searchTermParamName = urls.search.searchTermParamName,
                params = urls.search.params,
                template = urls.search.base,
                partnerCode = partnerCode,
                builderName = builder.name,
            )
        )
        urls.suggestions?.let { suggestions ->
            builder.suggestUrl =
                buildUrlWithParams(
                    searchTermParamName = suggestions.searchTermParamName,
                    params = suggestions.params,
                    template = suggestions.base,
                    partnerCode = partnerCode,
                    builderName = builder.name,
                )
        }
        urls.trending?.let { trending ->
            builder.trendingUrl =
                buildUrlWithParams(
                    searchTermParamName = trending.searchTermParamName,
                    params = trending.params,
                    template = trending.base,
                    partnerCode = partnerCode,
                    builderName = builder.name,
                )
        }
    }

    private fun buildUrlWithParams(
        searchTermParamName: String?,
        params: List<SearchUrlParam>,
        template: String,
        partnerCode: String?,
        builderName: String?,
    ): String {
        return buildString {
            val newParams = params.toMutableList()
            if (searchTermParamName != null && !template.contains("{searchTerms}")) {
                newParams.add(
                    SearchUrlParam(
                        searchTermParamName,
                        "{searchTerms}",
                        null,
                        null,
                    )
                )
            }
            append(readUriAPI(newParams, template, partnerCode))
            searchExtraParams?.let {
                with(it) {
                    if (builderName == searchEngineName) {
                        featureEnablerParam?.let { append("&$featureEnablerName=$it") }
                        append("&$channelIdName=$channelIdParam")
                    }
                }
            }
        }
    }

    @Throws(IllegalArgumentException::class)
    private fun readUriAPI(params: List<SearchUrlParam>, template: String, partnerCode: String?): Uri {
        require(template.isNotBlank()) { "URI cannot be blank" }
        val uriBuilder = template.toUri().buildUpon()
        for (param in params) {
            if (param.value == "{partnerCode}") {
                partnerCode?.let { uriBuilder.appendQueryParameter(param.name, it) }
            } else if (param.value == "{acceptLanguages}") {
                uriBuilder.appendQueryParameter(param.name, applicationContext.getAcceptLanguage())
            } else if (param.value != null) {
                uriBuilder.appendQueryParameter(param.name, param.value)
            }
        }
        return uriBuilder.build()
    }

    @SuppressWarnings("TooGenericExceptionCaught")
    private fun readImageAPI(
        attachmentModel: ByteArray?,
        mimetype: String,
        builder: SearchEngineBuilder,
        defaultIcon: Bitmap,
    ) {
        if (attachmentModel == null) {
            builder.icon = defaultIcon
            return
        }

        builder.icon =
            when (mimetype) {
                "image/svg+xml" ->
                    SvgIconDecoder()
                        .decode(
                            attachmentModel,
                            DesiredSize(TARGET_SIZE, TARGET_SIZE, MAX_SIZE, 2.0f),
                        ) ?: defaultIcon
                "image/x-icon" ->
                    ICOIconDecoder()
                        .decode(
                            attachmentModel,
                            DesiredSize(TARGET_SIZE, TARGET_SIZE, MAX_SIZE, 2.0f),
                        ) ?: defaultIcon
                "image/jpeg",
                "image/png" ->
                    BitmapFactory.decodeByteArray(
                        attachmentModel,
                        0,
                        attachmentModel.size,
                    ) ?: defaultIcon
                else -> defaultIcon
            }
    }

    private fun Context.getAcceptLanguage(): String {
        return LocaleManager.getCurrentLocale(this)?.toLanguageTag() ?: LocaleManager.getSystemDefault().toLanguageTag()
    }
}
