/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.middleware

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.feature.search.storage.BundledSearchEnginesStorage
import mozilla.components.feature.search.storage.CustomSearchEngineStorage
import mozilla.components.feature.search.storage.SearchEngineSelectorConfig
import mozilla.components.feature.search.storage.SearchEngineSelectorRepository
import mozilla.components.feature.search.storage.SearchMetadataStorage
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import java.util.Locale
import kotlin.coroutines.CoroutineContext

/**
 * Holds data for the search extra params.
 */
data class SearchExtraParams(
    val searchEngineName: String,
    val featureEnablerName: String?,
    val featureEnablerParam: String?,
    val channelIdName: String,
    val channelIdParam: String,
)

/**
 * [Middleware] implementation for loading and saving [SearchEngine]s whenever the state changes.
 *
 * @param additionalBundledSearchEngineIds List of (bundled) search engine IDs that will be loaded
 * in addition to the search engines for the user's region and made available through
 * [SearchState.additionalSearchEngines] and [SearchState.additionalSearchEngines].
 * @param migration Interface for a class that can provide data from a legacy system to be imported into the
 * storage used by the middleware.
 * @param customStorage A storage for custom search engines of the user.
 * @param bundleStorage A storage for loading bundled search engines.
 * @param metadataStorage A storage for saving additional metadata related to search.
 * @param searchExtraParams Optional search extra params.
 * @param ioDispatcher The coroutine dispatcher to be used when loading.
 */
class SearchMiddleware(
    context: Context,
    private val additionalBundledSearchEngineIds: List<String> = emptyList(),
    private val migration: Migration? = null,
    private val customStorage: CustomStorage = CustomSearchEngineStorage(context),
    private val bundleStorage: BundleStorage = BundledSearchEnginesStorage(context),
    private val metadataStorage: MetadataStorage = SearchMetadataStorage(
        context,
        additionalBundledSearchEngineIds.toSet(),
    ),
    private val searchEngineSelectorConfig: SearchEngineSelectorConfig? = null,
    private val searchExtraParams: SearchExtraParams? = null,
    private val ioDispatcher: CoroutineContext = Dispatchers.IO,
) : Middleware<BrowserState, BrowserAction> {
    private val logger = Logger("SearchMiddleware")
    private val scope = CoroutineScope(ioDispatcher)
    private val searchEngineSelectorRepository: SearchEngineRepository? =
        searchEngineSelectorConfig?.let { SearchEngineSelectorRepository(it) }

    override fun invoke(
        context: MiddlewareContext<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        when (action) {
            is SearchAction.SetRegionAction -> loadSearchEngines(context.store, action.regionState, action.distribution)
            is SearchAction.UpdateCustomSearchEngineAction -> saveCustomSearchEngine(action)
            is SearchAction.RemoveCustomSearchEngineAction -> removeCustomSearchEngine(action)
            is SearchAction.SelectSearchEngineAction -> updateSearchEngineSelection(action)
            else -> {
                // no-op
            }
        }

        next(action)

        when (action) {
            is SearchAction.ShowSearchEngineAction, is SearchAction.HideSearchEngineAction,
            is SearchAction.RestoreHiddenSearchEnginesAction,
            -> updateHiddenSearchEngines(context.state.search.hiddenSearchEngines)
            is SearchAction.AddAdditionalSearchEngineAction, is SearchAction.RemoveAdditionalSearchEngineAction ->
                updateAdditionalSearchEngines(context.state.search.additionalSearchEngines)
            is SearchAction.UpdateDisabledSearchEngineIdsAction -> updateDisabledSearchEngineIds(
                context.store,
                action,
            )
            else -> {
                // no-op
            }
        }
    }

    // SuppressWarnings will be removed when [BundleStorage] is entirely removed.
    @SuppressWarnings("LongMethod")
    private fun loadSearchEngines(
        store: Store<BrowserState, BrowserAction>,
        region: RegionState,
        distribution: String? = null,
    ) = scope.launch {
        val migrationValues = migration?.getValuesToMigrate()
        performCustomSearchEnginesMigration(migrationValues)

        val regionBundle = if (searchEngineSelectorRepository != null) {
            async(ioDispatcher) {
                searchEngineSelectorRepository.load(
                    region = region,
                    distribution = distribution,
                    searchExtraParams = searchExtraParams,
                    coroutineContext = ioDispatcher,
                )
            }
        } else {
            async(ioDispatcher) {
                bundleStorage.load(
                    region = region,
                    distribution = distribution,
                    searchExtraParams = searchExtraParams,
                    coroutineContext = ioDispatcher,
                )
            }
        }

        val customSearchEngines = async(ioDispatcher) { customStorage.loadSearchEngineList() }
        val hiddenSearchEngineIds = async(ioDispatcher) { metadataStorage.getHiddenSearchEngines() }
        val disabledSearchEngineIds = async(ioDispatcher) { metadataStorage.getDisabledSearchEngineIds() }
        val additionalSearchEngineIds = async(ioDispatcher) { metadataStorage.getAdditionalSearchEngines() }
        val allAdditionalSearchEngines = if (searchEngineSelectorRepository != null) {
            async(ioDispatcher) {
                regionBundle.await().list.filter { searchEngine ->
                    !searchEngine.isGeneral
                }
            }
        } else {
            async(ioDispatcher) {
                bundleStorage.load(
                    ids = additionalBundledSearchEngineIds,
                    searchExtraParams = searchExtraParams,
                    coroutineContext = ioDispatcher,
                )
            }
        }

        val hiddenSearchEngines = mutableListOf<SearchEngine>()
        val filteredRegionSearchEngines = regionBundle.await().list.filter { searchEngine ->
            if (hiddenSearchEngineIds.await().contains(searchEngine.id)) {
                hiddenSearchEngines.add(searchEngine)
                false
            } else {
                true
            }
        }

        val regionSearchEngineIds = regionBundle.await().list.map { searchEngine -> searchEngine.id }

        val additionalSearchEngines = allAdditionalSearchEngines.await().filter { searchEngine ->
            searchEngine.id in additionalSearchEngineIds.await() &&
                searchEngine.id !in regionSearchEngineIds
        }

        val additionalAvailableSearchEngines = allAdditionalSearchEngines.await().filter { searchEngine ->
            searchEngine.id !in additionalSearchEngineIds.await() &&
                searchEngine.id !in regionSearchEngineIds
        }

        performDefaultSearchEngineMigration(
            migrationValues,
            filteredRegionSearchEngines + customSearchEngines.await() + additionalSearchEngines,
        )
        val userChoice = async(ioDispatcher) { metadataStorage.getUserSelectedSearchEngine() }

        val action = SearchAction.SetSearchEnginesAction(
            regionSearchEngines = filteredRegionSearchEngines,
            regionDefaultSearchEngineId = regionBundle.await().defaultSearchEngineId,
            userSelectedSearchEngineId = userChoice.await()?.searchEngineId,
            userSelectedSearchEngineName = userChoice.await()?.searchEngineName,
            customSearchEngines = customSearchEngines.await(),
            hiddenSearchEngines = hiddenSearchEngines,
            disabledSearchEngineIds = disabledSearchEngineIds.await(),
            additionalSearchEngines = additionalSearchEngines,
            additionalAvailableSearchEngines = additionalAvailableSearchEngines,
            regionSearchEnginesOrder = regionSearchEngineIds,
        )
        store.dispatch(action)
    }

    private fun updateSearchEngineSelection(
        action: SearchAction.SelectSearchEngineAction,
    ) = scope.launch {
        metadataStorage.setUserSelectedSearchEngine(
            action.searchEngineId,
            action.searchEngineName,
        )
    }

    private fun removeCustomSearchEngine(
        action: SearchAction.RemoveCustomSearchEngineAction,
    ) = scope.launch {
        customStorage.removeSearchEngine(action.searchEngineId)
    }

    private fun saveCustomSearchEngine(
        action: SearchAction.UpdateCustomSearchEngineAction,
    ) = scope.launch {
        customStorage.saveSearchEngine(action.searchEngine)
    }

    private fun updateHiddenSearchEngines(
        hiddenSearchEngines: List<SearchEngine>,
    ) = scope.launch {
        metadataStorage.setHiddenSearchEngines(
            hiddenSearchEngines.map { searchEngine -> searchEngine.id },
        )
    }

    private fun updateAdditionalSearchEngines(
        additionalSearchEngines: List<SearchEngine>,
    ) = scope.launch {
        metadataStorage.setAdditionalSearchEngines(
            additionalSearchEngines.map { searchEngine -> searchEngine.id },
        )
    }

    private fun updateDisabledSearchEngineIds(
        store: Store<BrowserState, BrowserAction>,
        action: SearchAction.UpdateDisabledSearchEngineIdsAction,
    ) = scope.launch {
        val disabledIds = store.state.search.disabledSearchEngineIds
        val updatedList = if (action.isEnabled) {
            disabledIds - action.searchEngineId
        } else {
            disabledIds + action.searchEngineId
        }
        metadataStorage.setDisabledSearchEngineIds(updatedList)
    }

    private suspend fun performCustomSearchEnginesMigration(values: Migration.MigrationValues?) {
        if (values == null) {
            return
        }

        values.customSearchEngines.forEach { searchEngine ->
            customStorage.saveSearchEngine(searchEngine)
        }
    }

    private suspend fun performDefaultSearchEngineMigration(
        values: Migration.MigrationValues?,
        engines: List<SearchEngine>,
    ) {
        if (values == null) {
            return
        }

        val name = values.defaultSearchEngineName ?: return

        val default = engines.find { searchEngine ->
            searchEngine.name == name
        }

        if (default == null) {
            logger.error("Could not find migrated default search engine ($name)")
            return
        }

        metadataStorage.setUserSelectedSearchEngine(
            id = default.id,
            name = if (default.type == SearchEngine.Type.BUNDLED) {
                default.name
            } else {
                null
            },
        )
    }

    /**
     * A storage for custom search engines of the user.
     */
    interface CustomStorage {
        /**
         * Loads the list of search engines from the storage.
         */
        suspend fun loadSearchEngineList(): List<SearchEngine>

        /**
         * Removes the search engine with the specified [identifier] from the storage.
         */
        suspend fun removeSearchEngine(identifier: String)

        /**
         * Saves the given [searchEngine] to the storage. May replace an already existing search
         * engine with the same ID.
         */
        suspend fun saveSearchEngine(searchEngine: SearchEngine): Boolean
    }

    /**
     * A storage for loading bundled search engines.
     */
    interface BundleStorage {
        /**
         * Loads the bundled search engines for the given [locale] and [region].
         *
         * If [distribution] is not null then attempt to load the bundled search engine for the
         * [distribution] in the specified [locale] and [region] if available.
         */
        suspend fun load(
            region: RegionState,
            locale: Locale = Locale.getDefault(),
            distribution: String? = null,
            searchExtraParams: SearchExtraParams? = null,
            coroutineContext: CoroutineContext = Dispatchers.IO,
        ): Bundle

        /**
         * Loads the bundled search engines with the given [ids].
         */
        suspend fun load(
            ids: List<String>,
            searchExtraParams: SearchExtraParams? = null,
            coroutineContext: CoroutineContext = Dispatchers.IO,
        ): List<SearchEngine>

        /**
         * A loaded bundle containing the list of search engines and the ID of the default for
         * the region.
         */
        data class Bundle(
            val list: List<SearchEngine>,
            val defaultSearchEngineId: String,
        )
    }

    /**
     * A repository for search engines from Application Services.
     */
    interface SearchEngineRepository {
        /**
         * Loads the search engines for the given [locale] and [region] from Application Services.
         *
         * If [distribution] is not null then attempt to load the bundled search engine for the
         * [distribution] in the specified [locale] and [region] if available.
         */
        suspend fun load(
            region: RegionState,
            locale: Locale = Locale.getDefault(),
            distribution: String? = null,
            searchExtraParams: SearchExtraParams? = null,
            coroutineContext: CoroutineContext = Dispatchers.IO,
        ): BundleStorage.Bundle
    }

    /**
     * A storage for saving additional metadata related to search.
     */
    interface MetadataStorage {
        /**
         * Gets the ID (and optinally name) of the default search engine the user has picked. Returns
         * `null` if the user has not made a choice.
         */
        suspend fun getUserSelectedSearchEngine(): UserChoice?

        /**
         * Sets the ID (and optionally name) of the default search engine the user has picked.
         */
        suspend fun setUserSelectedSearchEngine(id: String, name: String?)

        /**
         * Sets the list of IDs of hidden search engines.
         */
        suspend fun setHiddenSearchEngines(ids: List<String>)

        /**
         * Gets the list of IDs of hidden search engines.
         */
        suspend fun getHiddenSearchEngines(): List<String>

        /**
         * Gets the list of IDs of additional search engines that the user explicitly added.
         */
        suspend fun getAdditionalSearchEngines(): List<String>

        /**
         * Sets the list of IDs of additional search engines that the user explicitly added.
         */
        suspend fun setAdditionalSearchEngines(ids: List<String>)

        /**
         * Gets the list of IDs of disabled search engine shortcuts.
         */
        suspend fun getDisabledSearchEngineIds(): List<String>

        /**
         * Sets the list of IDs of disabled search engine shortcuts.
         */
        suspend fun setDisabledSearchEngineIds(ids: List<String>)

        /**
         * Data class holding the ID and name of the selected search engine of the user.
         */
        data class UserChoice(
            val searchEngineId: String,
            val searchEngineName: String?,
        )
    }

    /**
     * Interface for a class that can provide data from a legacy system to be imported into the
     * storage used by the middleware.
     */
    interface Migration {
        /**
         * Returns the values to be migrated. It is expected that the application returns the values
         * only once. Afterwards the data is assumed to be migrated and should not be provided again.
         */
        fun getValuesToMigrate(): MigrationValues?

        /**
         * Holder data class for values to be migrated.
         *
         * @param customSearchEngines List of custom search engines that should be imported.
         * @param defaultSearchEngineName Name of the default search engine that the user had
         * selected. Or `null` if the user has not made any choice.
         */
        data class MigrationValues(
            val customSearchEngines: List<SearchEngine>,
            val defaultSearchEngineName: String?,
        )
    }
}
