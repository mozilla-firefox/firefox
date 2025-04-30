/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.storage

import mozilla.appservices.search.RefinedSearchConfig
import mozilla.appservices.search.SearchApiException
import mozilla.appservices.search.SearchApplicationName
import mozilla.appservices.search.SearchDeviceType
import mozilla.appservices.search.SearchEngineSelector
import mozilla.appservices.search.SearchUpdateChannel
import mozilla.appservices.search.SearchUserEnvironment
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.feature.search.icons.AttachmentModel
import mozilla.components.feature.search.icons.SearchConfigIconsModel
import mozilla.components.feature.search.icons.SearchConfigIconsUpdateService
import mozilla.components.feature.search.middleware.SearchExtraParams
import mozilla.components.feature.search.middleware.SearchMiddleware
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.remotesettings.RemoteSettingsService
import java.util.Locale
import kotlin.coroutines.CoroutineContext

/**
 * A repository implementation for loading and reading [SearchEngineDefinition]s from RemoteSettings.
 *
 * @param searchEngineSelectorConfig [SearchEngineSelectorConfig] holds configuration options for
 *          [SearchUserEnvironment]
 */
class SearchEngineSelectorRepository(
    private val searchEngineSelectorConfig: SearchEngineSelectorConfig,
) : SearchMiddleware.SearchEngineRepository {

    private val searchConfigIconsUpdateService: SearchConfigIconsUpdateService = SearchConfigIconsUpdateService()
    private val reader: SearchEngineReader = SearchEngineReader(type = SearchEngine.Type.BUNDLED)
    private val logger = Logger("SearchEngineSelectorRepository")

    init {
        try {
            searchEngineSelectorConfig.selector.useRemoteSettingsServer(
                service = searchEngineSelectorConfig.service.remoteSettingsService,
                applyEngineOverrides = false,
            )
        } catch (exception: SearchApiException) {
            logger.error("SearchEngineSelectorRepository failure SearchApiException $exception")
        }
    }

    /**
     * Load the [RefinedSearchConfig] for the given [region] and [locale].
     */
    @SuppressWarnings("TooGenericExceptionCaught")
    override suspend fun load(
        region: RegionState,
        locale: Locale,
        distribution: String?,
        searchExtraParams: SearchExtraParams?,
        coroutineContext: CoroutineContext,
    ): SearchMiddleware.BundleStorage.Bundle {
        try {
            val config = SearchUserEnvironment(
                locale = locale.toString(),
                region = region.toString(),
                experiment = searchEngineSelectorConfig.experiment,
                version = searchEngineSelectorConfig.appVersion,
                updateChannel = searchEngineSelectorConfig.updateChannel,
                distributionId = distribution ?: "",
                appName = searchEngineSelectorConfig.appName,
                deviceType = searchEngineSelectorConfig.deviceType,
            )
            val searchConfig = searchEngineSelectorConfig.selector.filterEngineConfiguration(config)

            val iconsList = searchConfigIconsUpdateService.fetchIcons(searchEngineSelectorConfig.service)

            val searchEngineList = mutableListOf<SearchEngine>()

            searchConfig.engines.forEach { engine ->
                val iconAttachmentModel = findMatchingIcon(engine.identifier, iconsList)
                iconAttachmentModel?.let {
                    val searchEngine = reader.loadStreamAPI(
                        engineDefinition = engine,
                        attachmentModel = it,
                    )
                    searchEngineList.add(searchEngine)
                }
            }

            return SearchMiddleware.BundleStorage.Bundle(searchEngineList, searchConfig.appDefaultEngineId ?: "")
        } catch (exception: Exception) {
            logger.error("exception in SearchEngineSelectorRepository.load")
        }
        return SearchMiddleware.BundleStorage.Bundle(emptyList(), "")
    }

    private fun findMatchingIcon(
        engineIdentifier: String,
        iconsList: List<SearchConfigIconsModel>,
    ): AttachmentModel? {
        iconsList.forEach { icon ->
            icon.engineIdentifier.forEach { patternIdPrefix ->
                if (engineIdentifier.startsWith(patternIdPrefix)) {
                    return icon.attachment
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
 */
data class SearchEngineSelectorConfig(
    val appName: SearchApplicationName,
    val appVersion: String,
    val deviceType: SearchDeviceType,
    val experiment: String,
    val updateChannel: SearchUpdateChannel,
    val selector: SearchEngineSelector,
    val service: RemoteSettingsService,
)
