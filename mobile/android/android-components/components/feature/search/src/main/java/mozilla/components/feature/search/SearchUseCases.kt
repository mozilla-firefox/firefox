/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search

import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.feature.search.ext.buildSearchUrl
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.base.log.logger.Logger

/** Contains use cases related to the search feature. */
class SearchUseCases(
    store: BrowserStore,
    tabsUseCases: TabsUseCases,
    sessionUseCases: SessionUseCases,
) {
    interface SearchUseCase {
        /** Triggers a search. */
        fun invoke(
            searchTerms: String,
            searchEngine: SearchEngine? = null,
            parentSessionId: String? = null,
        )
    }

    class DefaultSearchUseCase(
        private val store: BrowserStore,
        private val tabsUseCases: TabsUseCases,
        private val sessionUseCases: SessionUseCases,
    ) : SearchUseCase {
        private val logger = Logger("DefaultSearchUseCase")

        /** Triggers a search in the currently selected session. */
        override fun invoke(
            searchTerms: String,
            searchEngine: SearchEngine?,
            parentSessionId: String?,
        ) {
            invoke(searchTerms, store.state.selectedTabId, searchEngine)
        }

        /**
         * Triggers a search using the default search engine for the provided search terms.
         *
         * @param searchTerms the search terms.
         * @param sessionId the ID of the session/tab to use, or null if the currently selected tab should be used.
         * @param searchEngine Search Engine to use, or the default search engine if none is provided
         * @param flags Flags that will be used when loading the URL.
         * @param additionalHeaders The extra headers to use when loading the URL.
         * @param accessPoint The search access point the search is performed from, which selects the partner code to
         *   put in the search URL.
         */
        operator fun invoke(
            searchTerms: String,
            sessionId: String? = store.state.selectedTabId,
            searchEngine: SearchEngine? = null,
            flags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
            additionalHeaders: Map<String, String>? = null,
            accessPoint: String = SearchAccessPoint.DEFAULT,
        ) {
            val isTabPrivate =
                (sessionId ?: store.state.selectedTabId)?.let {
                    store.state.findTab(it)?.content?.private
                } ?: false
            val resolvedEngine = searchEngine ?: store.state.search.selectedOrDefaultSearchEngine(isTabPrivate)
            val searchUrl = resolvedEngine?.buildSearchUrl(searchTerms, accessPoint)

            if (searchUrl == null) {
                logger.warn("No default search engine available to perform search")
                return
            }

            val id =
                if (sessionId == null) {
                    // If no `sessionId` was passed in then create a new tab
                    tabsUseCases.addTab(
                        url = searchUrl,
                        flags = flags,
                        isSearch = true,
                        searchEngineName = searchEngine?.name,
                        additionalHeaders = additionalHeaders,
                    )
                } else {
                    // If we got a `sessionId` then try to find the tab and load the search URL in it
                    val existingTab = store.state.findTabOrCustomTab(sessionId)
                    if (existingTab != null) {
                        store.dispatch(ContentAction.UpdateIsSearchAction(existingTab.id, true, searchEngine?.name))
                        store.dispatch(
                            EngineAction.LoadUrlAction(
                                tabId = existingTab.id,
                                url = searchUrl,
                                flags = flags,
                                additionalHeaders = additionalHeaders,
                            )
                        )
                        existingTab.id
                    } else {
                        // If the tab with the provided id was not found then create a new tab
                        tabsUseCases.addTab(
                            url = searchUrl,
                            isSearch = true,
                            searchEngineName = searchEngine?.name,
                            flags = flags,
                            additionalHeaders = additionalHeaders,
                        )
                    }
                }

            store.dispatch(ContentAction.UpdateSearchTermsAction(id, searchTerms))
        }
    }

    class NewTabSearchUseCase(
        private val store: BrowserStore,
        private val tabsUseCases: TabsUseCases,
        private val isPrivate: Boolean,
    ) : SearchUseCase {
        private val logger = Logger("NewTabSearchUseCase")

        override fun invoke(
            searchTerms: String,
            searchEngine: SearchEngine?,
            parentSessionId: String?,
        ) {
            invoke(
                searchTerms,
                source = SessionState.Source.Internal.None,
                selected = true,
                searchEngine = searchEngine,
                parentSessionId = parentSessionId,
            )
        }

        /**
         * Triggers a search on a new session, using the default search engine for the provided search terms.
         *
         * @param searchTerms the search terms.
         * @param selected whether or not the new session should be selected, defaults to true.
         * @param source the source of the new session.
         * @param searchEngine Search Engine to use, or the default search engine if none is provided
         * @param parentSessionId optional parent session to attach this new search session to
         * @param flags Flags that will be used when loading the URL.
         * @param additionalHeaders The extra headers to use when loading the URL.
         * @param accessPoint The search access point the search is performed from, which selects the partner code to
         *   put in the search URL.
         */
        operator fun invoke(
            searchTerms: String,
            source: SessionState.Source,
            selected: Boolean = true,
            searchEngine: SearchEngine? = null,
            parentSessionId: String? = null,
            flags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
            additionalHeaders: Map<String, String>? = null,
            accessPoint: String = SearchAccessPoint.DEFAULT,
        ) {
            val resolvedEngine = searchEngine ?: store.state.search.selectedOrDefaultSearchEngine(isPrivate)
            val searchUrl = resolvedEngine?.buildSearchUrl(searchTerms, accessPoint)

            if (searchUrl == null) {
                logger.warn("No default search engine available to perform search")
                return
            }

            val id =
                tabsUseCases.addTab(
                    url = searchUrl,
                    parentId = parentSessionId,
                    flags = flags,
                    source = source,
                    selectTab = selected,
                    private = isPrivate,
                    isSearch = true,
                    additionalHeaders = additionalHeaders,
                )

            store.dispatch(ContentAction.UpdateSearchTermsAction(id, searchTerms))
        }
    }

    /** Adds a new search engine to the list of search engines the user can use for searches. */
    class AddNewSearchEngineUseCase(private val store: BrowserStore) {
        /** Adds the given [searchEngine] to the list of search engines the user can use for searches. */
        operator fun invoke(searchEngine: SearchEngine) {
            when (searchEngine.type) {
                SearchEngine.Type.BUNDLED -> store.dispatch(SearchAction.ShowSearchEngineAction(searchEngine.id))

                SearchEngine.Type.BUNDLED_ADDITIONAL ->
                    store.dispatch(SearchAction.AddAdditionalSearchEngineAction(searchEngine.id))

                SearchEngine.Type.CUSTOM -> store.dispatch(SearchAction.UpdateCustomSearchEngineAction(searchEngine))

                SearchEngine.Type.APPLICATION -> {
                    /* Do nothing */
                }
            }
        }
    }

    /** Removes a search engine from the list of search engines the user can use for searches. */
    class RemoveExistingSearchEngineUseCase(private val store: BrowserStore) {
        /** Removes the given [searchEngine] from the list of search engines the user can use for searches. */
        operator fun invoke(searchEngine: SearchEngine) {
            when (searchEngine.type) {
                SearchEngine.Type.BUNDLED -> store.dispatch(SearchAction.HideSearchEngineAction(searchEngine.id))

                SearchEngine.Type.BUNDLED_ADDITIONAL ->
                    store.dispatch(SearchAction.RemoveAdditionalSearchEngineAction(searchEngine.id))

                SearchEngine.Type.CUSTOM -> store.dispatch(SearchAction.RemoveCustomSearchEngineAction(searchEngine.id))

                SearchEngine.Type.APPLICATION -> {
                    /* Do nothing */
                }
            }
        }
    }

    /** Marks a search engine as "selected" by the user to be the default search engine to perform searches with. */
    class SelectSearchEngineUseCase(private val store: BrowserStore) {
        /**
         * Marks the given [searchEngine] as "selected" by the user to be the default search engine to perform searches
         * with.
         */
        operator fun invoke(searchEngine: SearchEngine) {
            val name =
                if (searchEngine.type == SearchEngine.Type.BUNDLED) {
                    // For bundled search engines we additionally save the name of the search engine.
                    // We do this because with "home" region changes the previous search plugin/id
                    // may no longer be available, but there may be a clone of the search engine with
                    // a different plugin/id using the same name.
                    // This should be safe to do since Fenix as well as Fennec only kept the name of
                    // the default search engine.
                    // For all other cases (e.g. custom search engines) we only care about the ID and
                    // do not want to switch to a different search engine based on its name once it is
                    // gone.
                    searchEngine.name
                } else {
                    null
                }

            store.dispatch(SearchAction.SelectSearchEngineAction(searchEngine.id, name))
        }
    }

    /** Marks a search engine as the default for private browsing. */
    class SelectPrivateSearchEngineUseCase(private val store: BrowserStore) {
        /**
         * Marks the given [searchEngine] as "selected" by the user to be the default search engine to perform searches
         * with in private browsing tab sessions.
         */
        operator fun invoke(searchEngine: SearchEngine) {
            val name =
                if (searchEngine.type == SearchEngine.Type.BUNDLED) {
                    searchEngine.name
                } else {
                    null
                }

            store.dispatch(SearchAction.SelectPrivateSearchEngineAction(searchEngine.id, name))
        }
    }

    /** Clears the private browsing search engine override, falling back to the normal default. */
    class ClearPrivateSearchEngineUseCase(private val store: BrowserStore) {
        /**
         * Clears the private browsing default [SearchEngine] and sets the private default search to be the same as the
         * normal default search engine.
         */
        operator fun invoke() {
            store.dispatch(SearchAction.ClearPrivateSearchEngineAction)
        }
    }

    /** Updates the list of unselected shortcuts, to be hidden from the quick search menus. */
    class UpdateDisabledSearchEngineIdsUseCase(private val store: BrowserStore) {
        /**
         * Updates the list of unselected shortcuts with the given [searchEngineId], to be hidden from the quick search
         * menus.
         */
        operator fun invoke(
            searchEngineId: String,
            isEnabled: Boolean,
        ) {
            store.dispatch(SearchAction.UpdateDisabledSearchEngineIdsAction(searchEngineId, isEnabled))
        }
    }

    /** Restores bundled search engines that may have been removed. */
    class RestoreHiddenSearchEnginesUseCase(private val store: BrowserStore) {
        /** Restores all hidden engines back to the bundled engine list. */
        operator fun invoke() {
            store.dispatch(SearchAction.RestoreHiddenSearchEnginesAction)
        }
    }

    val defaultSearch: DefaultSearchUseCase by lazy {
        DefaultSearchUseCase(store, tabsUseCases, sessionUseCases)
    }

    val newTabSearch: NewTabSearchUseCase by lazy {
        NewTabSearchUseCase(store, tabsUseCases, false)
    }

    val newPrivateTabSearch: NewTabSearchUseCase by lazy {
        NewTabSearchUseCase(store, tabsUseCases, true)
    }

    val addSearchEngine: AddNewSearchEngineUseCase by lazy {
        AddNewSearchEngineUseCase(store)
    }

    val removeSearchEngine: RemoveExistingSearchEngineUseCase by lazy {
        RemoveExistingSearchEngineUseCase(store)
    }

    val selectSearchEngine: SelectSearchEngineUseCase by lazy {
        SelectSearchEngineUseCase(store)
    }

    val selectPrivateSearchEngine: SelectPrivateSearchEngineUseCase by lazy {
        SelectPrivateSearchEngineUseCase(store)
    }

    val clearPrivateSearchEngine: ClearPrivateSearchEngineUseCase by lazy {
        ClearPrivateSearchEngineUseCase(store)
    }

    val updateDisabledSearchEngineIds: UpdateDisabledSearchEngineIdsUseCase by lazy {
        UpdateDisabledSearchEngineIdsUseCase(store)
    }

    val restoreHiddenSearchEngines: RestoreHiddenSearchEnginesUseCase by lazy {
        RestoreHiddenSearchEnginesUseCase(store)
    }
}
