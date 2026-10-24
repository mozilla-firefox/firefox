/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.storage

import android.content.Context
import android.graphics.Bitmap
import java.util.Locale
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.withContext
import mozilla.appservices.remotesettings.RemoteSettingsClient
import mozilla.appservices.remotesettings.RemoteSettingsRecord
import mozilla.appservices.search.SearchApiException
import mozilla.appservices.search.SearchEngineSelector
import mozilla.appservices.search.SearchUserEnvironment
import mozilla.appservices.search.SearchUserEnvironmentV3
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.feature.search.SearchApplicationName
import mozilla.components.feature.search.SearchDeviceType
import mozilla.components.feature.search.SearchUpdateChannel
import mozilla.components.feature.search.icons.SearchConfigIconsParser
import mozilla.components.feature.search.icons.SearchConfigIconsUpdateService
import mozilla.components.feature.search.into
import mozilla.components.feature.search.intoV3
import mozilla.components.feature.search.middleware.SearchExtraParams
import mozilla.components.feature.search.middleware.SearchMiddleware.BundleStorage.Bundle
import mozilla.components.feature.search.middleware.SearchMiddleware.SearchEngineRepository
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.remotesettings.RemoteSettingsService

/**
 * A repository implementation for loading and reading [SearchEngineDefinition]s from RemoteSettings.
 *
 * @param context [Context] used by [SearchEngineReader] to resolve the current locale.
 * @param searchEngineSelectorConfig [SearchEngineSelectorConfig] holds configuration options for
 *   [SearchUserEnvironment].
 * @param defaultSearchEngineIcon The [Bitmap] to use as the fallback icon for any search engine when a specific remote
 *   icon cannot be found or loaded.
 * @param client Optional [RemoteSettingsClient] for fetching search engine definitions.
 * @param selector The [SearchEngineSelector] which manages and applies search configuration
 * @param searchConfigIconsUpdateService The [SearchConfigIconsUpdateService] which fetches the search engine icons.
 */
class SearchEngineSelectorRepository(
    context: Context,
    private val searchEngineSelectorConfig: SearchEngineSelectorConfig,
    private val defaultSearchEngineIcon: Bitmap,
    client: RemoteSettingsClient?,
    private val selector: SearchEngineSelector = SearchEngineSelector(),
    private val searchConfigIconsUpdateService: SearchConfigIconsUpdateService = SearchConfigIconsUpdateService(client),
) : SearchEngineRepository {

    private val reader: SearchEngineReader = SearchEngineReader(context = context, type = SearchEngine.Type.BUNDLED)
    private val logger = Logger("SearchEngineSelectorRepository")
    private val parser = SearchConfigIconsParser()

    init {
        try {
            if (searchEngineSelectorConfig.useSearchConfigV3) {
                selector.useRemoteSettingsServerV3(service = searchEngineSelectorConfig.service.remoteSettingsService)
            } else {
                selector.useRemoteSettingsServer(
                    service = searchEngineSelectorConfig.service.remoteSettingsService,
                    applyEngineOverrides = false,
                )
            }
        } catch (exception: SearchApiException) {
            logger.error("SearchEngineSelectorRepository failure SearchApiException $exception")
        }
    }

    override fun computeNewSearchEnvironmentId(
        region: RegionState,
        locale: Locale,
        distribution: String?,
    ): Int =
        if (searchEngineSelectorConfig.useSearchConfigV3) {
            buildEnvironmentV3(region, locale, distribution).hashCode()
        } else {
            buildEnvironment(region, locale, distribution).hashCode()
        }

    /** Load the refined search configuration for the given [region] and [locale]. */
    @SuppressWarnings("TooGenericExceptionCaught")
    override suspend fun load(
        region: RegionState,
        locale: Locale,
        distribution: String?,
        searchExtraParams: SearchExtraParams?,
        coroutineContext: CoroutineContext,
    ): Bundle =
        withContext(coroutineContext) {
            try {
                return@withContext if (searchEngineSelectorConfig.useSearchConfigV3) {
                    loadV3(region, locale, distribution)
                } else {
                    loadV2(region, locale, distribution)
                }
            } catch (_: Exception) {
                logger.error("exception in SearchEngineSelectorRepository.load")
            }
            return@withContext Bundle(
                list = emptyList(),
                defaultSearchEngineId = "",
                searchEnvironmentId = null,
            )
        }

    private fun loadV2(region: RegionState, locale: Locale, distribution: String?): Bundle {
        val config = buildEnvironment(region, locale, distribution)
        val searchConfig = selector.filterEngineConfiguration(config)
        val iconsList = searchConfigIconsUpdateService.fetchIconsRecords(searchEngineSelectorConfig.service)
        val searchEngineList =
            buildSearchEngineList(
                engines = searchConfig.engines,
                iconsList = iconsList,
                identifierOf = { it.identifier },
                readEngine = { engine, attachmentModel, mimetype ->
                    reader.loadStreamAPI(
                        engineDefinition = engine,
                        attachmentModel = attachmentModel,
                        mimetype = mimetype,
                        defaultIcon = defaultSearchEngineIcon,
                    )
                },
            )

        return Bundle(
            list = searchEngineList,
            defaultSearchEngineId = searchConfig.appDefaultEngineId ?: searchConfig.engines.first().identifier,
            searchEnvironmentId = config.hashCode(),
        )
    }

    private fun loadV3(region: RegionState, locale: Locale, distribution: String?): Bundle {
        val config = buildEnvironmentV3(region, locale, distribution)
        val searchConfig = selector.filterEngineConfigurationV3(config)
        val iconsList = searchConfigIconsUpdateService.fetchIconsRecords(searchEngineSelectorConfig.service)
        val searchEngineList =
            buildSearchEngineList(
                engines = searchConfig.engines,
                iconsList = iconsList,
                identifierOf = { it.identifier },
                readEngine = { engine, attachmentModel, mimetype ->
                    reader.loadStreamAPIV3(
                        engineDefinition = engine,
                        attachmentModel = attachmentModel,
                        mimetype = mimetype,
                        defaultIcon = defaultSearchEngineIcon,
                    )
                },
            )

        return Bundle(
            list = searchEngineList,
            defaultSearchEngineId = searchConfig.appDefaultEngineId ?: searchConfig.engines.first().identifier,
            searchEnvironmentId = config.hashCode(),
        )
    }

    private fun buildEnvironment(
        region: RegionState,
        locale: Locale,
        distribution: String?,
    ) =
        SearchUserEnvironment(
            locale = locale.languageTag,
            region = region.home,
            experiment = searchEngineSelectorConfig.experiment,
            version = searchEngineSelectorConfig.appVersion,
            updateChannel = searchEngineSelectorConfig.updateChannel.into(),
            distributionId = distribution ?: "",
            appName = searchEngineSelectorConfig.appName.into(),
            deviceType = searchEngineSelectorConfig.deviceType.into(),
        )

    private fun buildEnvironmentV3(
        region: RegionState,
        locale: Locale,
        distribution: String?,
    ) =
        SearchUserEnvironmentV3(
            locale = locale.languageTag,
            region = region.home,
            experiment = searchEngineSelectorConfig.experiment,
            version = searchEngineSelectorConfig.appVersion,
            updateChannel = searchEngineSelectorConfig.updateChannel.intoV3(),
            distributionId = distribution ?: "",
            appName = searchEngineSelectorConfig.appName.intoV3(),
            deviceType = searchEngineSelectorConfig.deviceType.intoV3(),
        )

    /**
     * @param engines The engine definitions from the configuration, of whichever search-config version is in use.
     * @param iconsList The icon records to match the engines against.
     * @param identifierOf Returns the identifier of an engine definition, used to find its icon.
     * @param readEngine Converts an engine definition and its icon attachment into a [SearchEngine].
     */
    private fun <T> buildSearchEngineList(
        engines: List<T>,
        iconsList: List<RemoteSettingsRecord>,
        identifierOf: (T) -> String,
        readEngine: (engine: T, attachmentModel: ByteArray?, mimetype: String) -> SearchEngine,
    ): List<SearchEngine> {
        val searchEngineList = mutableListOf<SearchEngine>()
        engines.forEach { engine ->
            val iconAttachmentModel = findMatchingIcon(identifierOf(engine), iconsList)
            val searchEngine =
                try {
                    readEngine(
                        engine,
                        searchConfigIconsUpdateService.fetchIconAttachment(iconAttachmentModel),
                        iconAttachmentModel?.attachment?.mimetype ?: "",
                    )
                } catch (_: IllegalArgumentException) {
                    return@forEach
                }
            searchEngineList.add(searchEngine)
        }
        return searchEngineList
    }

    private fun findMatchingIcon(
        engineIdentifier: String,
        iconsList: List<RemoteSettingsRecord>,
    ): RemoteSettingsRecord? {
        iconsList.forEach { icon ->
            val parsedIcon = parser.parseRecord(icon)
            parsedIcon?.engineIdentifier?.forEach {
                val prefix =
                    if (it.endsWith("*", true)) {
                        it.removeSuffix("*")
                    } else {
                        it
                    }
                if (engineIdentifier.startsWith(prefix)) {
                    return icon
                }
            }
        }
        return null
    }
}

/**
 * Data class for passing app information to [SearchUserEnvironment].
 *
 * @param appName The [SearchApplicationName] for the app.
 * @param appVersion A [String] representing the version of the app.
 * @param deviceType [SearchDeviceType] for device running the app.
 * @param experiment A [String] ID for the experiment for the app.
 * @param updateChannel [SearchUpdateChannel] for the build variant of the app.
 * @param useSearchConfigV3 Whether to read the configuration from the `search-config-v3` collection instead of
 *   `search-config-v2`.
 */
data class SearchEngineSelectorConfig(
    val appName: SearchApplicationName,
    val appVersion: String,
    val deviceType: SearchDeviceType,
    val experiment: String,
    val updateChannel: SearchUpdateChannel,
    val service: RemoteSettingsService,
    val useSearchConfigV3: Boolean = false,
)

private val Locale.languageTag: String
    get() = "$language-$country"
