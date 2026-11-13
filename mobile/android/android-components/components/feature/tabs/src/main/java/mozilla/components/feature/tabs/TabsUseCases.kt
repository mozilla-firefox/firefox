/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabs

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.browser.session.storage.RecoverableBrowserState
import mozilla.components.browser.session.storage.SessionStorage
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.RestoreCompleteAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.action.TabListAction.RestoreAction.RestoreLocation
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.action.UndoAction
import mozilla.components.browser.state.selector.findNormalOrPrivateTabByUrl
import mozilla.components.browser.state.selector.findNormalOrPrivateTabByUrlIgnoringFragment
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.browser.state.state.recover.TabState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.concept.engine.EngineSessionStateStorage
import mozilla.components.concept.storage.HistoryMetadataKey
import mozilla.components.feature.session.SessionUseCases.LoadUrlUseCase

/**
 * Contains use cases related to the tabs feature.
 *
 * Note: Please instead route tab data side effects through
 * [mozilla.components.feature.tabdata.coordinator.TabDataCoordinator] for applications which implement tab groups via
 * [mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository].
 */
class TabsUseCases(
    store: BrowserStore,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    /** Contract for use cases that select a tab. */
    interface SelectTabUseCase {
        /** Select tab with the given [tabId]. */
        operator fun invoke(tabId: String)
    }

    class DefaultSelectTabUseCase internal constructor(private val store: BrowserStore) : SelectTabUseCase {
        /** Marks the tab with the provided [tabId] as selected. */
        override fun invoke(tabId: String) {
            store.dispatch(TabListAction.SelectTabAction(tabId))
        }
    }

    /** Contract for use cases that remove a tab. */
    interface RemoveTabUseCase {
        /**
         * Removes the session with the provided ID. This method has no effect if the session doesn't exist.
         *
         * @param tabId The ID of the session to remove.
         * @param excludedFallbackTabIds A list of tab IDs that should be excluded from being the next selected tab once
         *   the requested tabs are removed.
         */
        operator fun invoke(tabId: String, excludedFallbackTabIds: Set<String> = emptySet())

        /**
         * Removes the session with the provided ID. This method has no effect if the session doesn't exist.
         *
         * @param tabId The ID of the session to remove.
         * @param selectParentIfExists Whether or not to select the parent tab of the removed tab if a parent exists.
         *   Note that the default implementation of this method will ignore [selectParentIfExists] and never select a
         *   parent. This is a temporary workaround to prevent additional API breakage for subtypes other than
         *   [DefaultRemoveTabUseCase]. The default implementation should be removed together with invoke(Session).
         * @param excludedFallbackTabIds A list of tab IDs that should be excluded from being the next selected tab once
         *   the requested tabs are removed.
         */
        operator fun invoke(
            tabId: String,
            selectParentIfExists: Boolean,
            excludedFallbackTabIds: Set<String> = emptySet(),
        ) = invoke(tabId, excludedFallbackTabIds)
    }

    /** Default implementation of [RemoveTabUseCase]. */
    class DefaultRemoveTabUseCase internal constructor(private val store: BrowserStore) : RemoveTabUseCase {

        /**
         * Removes the tab with the provided ID. This method has no effect if the tab doesn't exist.
         *
         * @param tabId The ID of the tab to remove.
         * @param excludedFallbackTabIds A list of tab IDs that should be excluded from being the next selected tab once
         *   the requested tabs are removed.
         */
        override operator fun invoke(tabId: String, excludedFallbackTabIds: Set<String>) {
            store.dispatch(TabListAction.RemoveTabAction(tabId, excludedFallbackTabIds = excludedFallbackTabIds))
        }

        /**
         * Removes the session with the provided ID. This method has no effect if the session doesn't exist.
         *
         * @param tabId The ID of the session to remove.
         * @param selectParentIfExists Whether or not to select the parent tab of the removed tab if a parent exists.
         * @param excludedFallbackTabIds A list of tab IDs that should be excluded from being the next selected tab once
         *   the requested tabs are removed.
         */
        override operator fun invoke(
            tabId: String,
            selectParentIfExists: Boolean,
            excludedFallbackTabIds: Set<String>,
        ) {
            store.dispatch(TabListAction.RemoveTabAction(tabId, selectParentIfExists, excludedFallbackTabIds))
        }
    }

    class AddNewTabUseCase internal constructor(private val store: BrowserStore) : LoadUrlUseCase {

        /**
         * Adds a new tab and loads the provided URL.
         *
         * @param url The URL to be loaded in the new tab.
         * @param flags the [LoadUrlFlags] to use when loading the provided URL.
         * @param originalInput If the user entered a URL, this is the original user input before any fixups were
         *   applied to it.
         */
        override operator fun invoke(
            url: String,
            flags: LoadUrlFlags,
            additionalHeaders: Map<String, String>?,
            originalInput: String?,
        ) {
            this.invoke(
                url,
                selectTab = true,
                startLoading = true,
                parentId = null,
                flags = flags,
                originalInput = originalInput,
            )
        }

        /**
         * Adds a new tab and loads the provided URL.
         *
         * @param url The URL to be loaded in the new tab.
         * @param selectTab True (default) if the new tab should be selected immediately.
         * @param startLoading True (default) if the new tab should start loading immediately.
         * @param parentId the id of the parent tab to use for the newly created tab.
         * @param flags the [LoadUrlFlags] to use when loading the provided URL.
         * @param contextId the session context id to use for this tab.
         * @param title The title of the new page.
         * @param engineSession (optional) engine session to use for this tab.
         * @param source The [SessionState.Source] of the new tab.
         * @param searchTerms The search terms of this new tab if it represents an active search (result) page.
         * @param private Whether or not the new tab should be private.
         * @param historyMetadata the [HistoryMetadataKey] of the new tab in case this tab was opened from history.
         * @param isSearch whether or not the provided URL is the result of a search.
         * @param searchEngineName The search engine name.
         * @param additionalHeaders The extra headers to use when loading the provided URL.
         * @param originalInput If the user entered a URL, this is the original user input before any fixups were
         *   applied to it.
         * @param textDirectiveUserActivation whether loading allows the scroll by text fragmentation.
         * @return The ID of the created tab.
         */
        operator fun invoke(
            url: String = "about:blank",
            selectTab: Boolean = true,
            startLoading: Boolean = true,
            parentId: String? = null,
            flags: LoadUrlFlags = LoadUrlFlags.none(),
            contextId: String? = null,
            title: String = "",
            engineSession: EngineSession? = null,
            source: SessionState.Source = SessionState.Source.Internal.NewTab,
            searchTerms: String = "",
            private: Boolean = false,
            historyMetadata: HistoryMetadataKey? = null,
            isSearch: Boolean = false,
            searchEngineName: String? = null,
            additionalHeaders: Map<String, String>? = null,
            originalInput: String? = null,
            textDirectiveUserActivation: Boolean = false,
        ): String {
            val tab =
                createTab(
                    url = url,
                    private = private,
                    source = source,
                    contextId = contextId,
                    parent = parentId?.let { store.state.findTab(it) },
                    title = title,
                    engineSession = engineSession,
                    searchTerms = searchTerms,
                    initialLoadFlags = flags,
                    initialAdditionalHeaders = additionalHeaders,
                    historyMetadata = historyMetadata,
                    desktopMode = store.state.desktopMode,
                    originalInput = originalInput,
                    initialTextDirectiveUserActivation = textDirectiveUserActivation,
                )

            store.dispatch(TabListAction.AddTabAction(tab, select = selectTab))

            store.dispatch(ContentAction.UpdateIsSearchAction(tab.id, isSearch, searchEngineName))

            // If an engine session is specified then loading will have already started when linking
            // the tab to its engine session. Otherwise we ask to load the URL here.
            if (startLoading && engineSession == null) {
                store.dispatch(
                    EngineAction.LoadUrlAction(
                        tabId = tab.id,
                        url = url,
                        flags = flags,
                        additionalHeaders = additionalHeaders,
                        includeParent = true,
                        textDirectiveUserActivation = textDirectiveUserActivation,
                    )
                )
            }

            return tab.id
        }
    }

    /** Use case for removing a list of tabs. */
    class RemoveTabsUseCase internal constructor(private val store: BrowserStore) {
        /**
         * Removes a specified list of tabs.
         *
         * @param ids The IDs of the tabs to remove.
         * @param excludedFallbackTabIds A list of tab IDs that should be excluded from being the next selected tab once
         *   the requested tabs are removed.
         */
        operator fun invoke(ids: List<String>, excludedFallbackTabIds: Set<String> = emptySet()) {
            store.dispatch(TabListAction.RemoveTabsAction(ids, excludedFallbackTabIds))
        }
    }

    class RemoveAllTabsUseCase internal constructor(private val store: BrowserStore) {
        /**
         * Removes all tabs.
         *
         * @param recoverable Indicates whether removed tabs should be recoverable.
         */
        operator fun invoke(recoverable: Boolean = true) {
            store.dispatch(TabListAction.RemoveAllTabsAction(recoverable))
        }
    }

    /** Use case for removing all normal (non-private) tabs. */
    class RemoveNormalTabsUseCase internal constructor(private val store: BrowserStore) {
        /** Removes all normal (non-private) tabs. */
        operator fun invoke() {
            store.dispatch(TabListAction.RemoveAllNormalTabsAction)
        }
    }

    /** Use case for removing all private tabs. */
    class RemovePrivateTabsUseCase internal constructor(private val store: BrowserStore) {
        /** Removes all private tabs. */
        operator fun invoke() {
            store.dispatch(TabListAction.RemoveAllPrivateTabsAction)
        }
    }

    /** Use case for restoring removed tabs ("undo"). */
    class UndoTabRemovalUseCase(private val store: BrowserStore) {
        /** Restores the list of tabs in the undo history. */
        operator fun invoke() {
            store.dispatch(UndoAction.RestoreRecoverableTabs)
        }
    }

    /** Use case for restoring tabs. */
    class RestoreUseCase(
        val store: BrowserStore,
        private val selectTab: SelectTabUseCase,
        private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
        private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
        private val currentTimeMillis: () -> Long = { System.currentTimeMillis() },
    ) {
        /**
         * Restores the given list of [RecoverableTab]s.
         *
         * @param tabs The list of tabs to restore.
         * @param selectTabId The ID of the selected tab in [tabs]. Or `null` if no selection was restored.
         * @param restoreLocation [RestoreLocation] indicating where to restore [tabs].
         */
        operator fun invoke(
            tabs: List<RecoverableTab>,
            selectTabId: String? = null,
            restoreLocation: RestoreLocation = RestoreLocation.END,
        ) {
            store.dispatch(
                TabListAction.RestoreAction(
                    tabs = tabs,
                    selectedTabId = selectTabId,
                    restoreLocation = restoreLocation,
                )
            )
        }

        /**
         * Restores the given [RecoverableBrowserState].
         *
         * @param state The [RecoverableBrowserState] to be restored.
         * @param restoreLocation [RestoreLocation] indicating where to restore [state].
         */
        operator fun invoke(
            state: RecoverableBrowserState,
            restoreLocation: RestoreLocation = RestoreLocation.END,
        ) {
            state.isTranslationsEngineSupported?.let { isEngineSupported ->
                store.dispatch(TranslationsAction.SetEngineSupportedAction(isEngineSupported))
            }
            invoke(
                tabs = state.tabs,
                selectTabId = state.selectedTabId,
                restoreLocation = restoreLocation,
            )
        }

        /**
         * Restores the browsing session from the given [SessionStorage]. Also dispatches [RestoreCompleteAction] on the
         * [BrowserStore] once restore has been completed.
         *
         * @param storage the [SessionStorage] to restore state from.
         * @param tabTimeoutInMs the amount of time in milliseconds after which inactive tabs will be discarded and not
         *   restored. Defaults to Long.MAX_VALUE, meaning all tabs will be restored by default.
         */
        suspend operator fun invoke(
            storage: SessionStorage,
            tabTimeoutInMs: Long = Long.MAX_VALUE,
        ) =
            withContext(ioDispatcher) {
                val now = currentTimeMillis()
                val state = storage.restore {
                    val lastActiveTime = maxOf(it.state.lastAccess, it.state.createdAt)
                    now - lastActiveTime <= tabTimeoutInMs
                }
                if (state != null) {
                    withContext(mainDispatcher) {
                        invoke(
                            state = state,
                            restoreLocation = RestoreLocation.BEGINNING,
                        )
                    }
                }
                store.dispatch(RestoreCompleteAction)
            }

        /** Restores the given [TabState] and updates the selected tab if [updateSelection] is `true`. */
        suspend operator fun invoke(
            tab: TabState,
            engineStateReader: EngineSessionStateStorage,
            updateSelection: Boolean = true,
        ) =
            withContext(ioDispatcher) {
                val recoverableTab =
                    RecoverableTab(
                        state = tab,
                        engineSessionState = engineStateReader.read(tab.id),
                    )
                invoke(listOf(recoverableTab))

                if (updateSelection) {
                    selectTab(recoverableTab.state.id)
                }
            }
    }

    /** Use case for selecting an existing tab or creating a new tab with a specific URL. */
    class SelectOrAddUseCase(private val store: BrowserStore) {
        /**
         * Selects an already existing tab with the matching [HistoryMetadataKey] or otherwise creates a new tab with
         * the given [url].
         *
         * @param url The URL to be selected or loaded in the new tab.
         * @param historyMetadata The [HistoryMetadataKey] to match for existing tabs.
         * @return The ID of the selected or created tab.
         */
        operator fun invoke(
            url: String,
            historyMetadata: HistoryMetadataKey,
        ): String {
            val tab = store.state.tabs.find { it.historyMetadata == historyMetadata }

            return if (tab != null) {
                store.dispatch(TabListAction.SelectTabAction(tab.id))
                tab.id
            } else {
                this.invoke(url)
            }
        }

        /**
         * Selects an already existing tab displaying [url] or otherwise creates a new tab.
         *
         * @param url The URL to be loaded in the new tab.
         * @param private Whether or not this session should use private mode.
         * @param source The origin of a session to describe how and why it was created.
         * @param flags The [LoadUrlFlags] to use when loading the provided URL.
         * @param ignoreFragment Whether to ignore the fragment identifier of the url while comparing with existing
         *   tabs.
         * @return The ID of the selected or created tab.
         */
        operator fun invoke(
            url: String,
            private: Boolean = false,
            source: SessionState.Source = SessionState.Source.Internal.NewTab,
            flags: LoadUrlFlags = LoadUrlFlags.none(),
            ignoreFragment: Boolean = false,
        ): String {
            val existingTab =
                if (ignoreFragment) {
                    store.state.findNormalOrPrivateTabByUrlIgnoringFragment(url, private)
                } else {
                    store.state.findNormalOrPrivateTabByUrl(url, private)
                }

            return if (existingTab != null) {
                store.dispatch(TabListAction.SelectTabAction(existingTab.id))
                existingTab.id
            } else {
                val tab =
                    createTab(
                        url = url,
                        private = private,
                        source = source,
                        initialLoadFlags = flags,
                        desktopMode = store.state.desktopMode,
                    )
                store.dispatch(TabListAction.AddTabAction(tab, select = true))
                store.dispatch(
                    EngineAction.LoadUrlAction(
                        tab.id,
                        url,
                        flags,
                        includeParent = true,
                    )
                )
                tab.id
            }
        }
    }

    /** Use case for duplicating a tab. */
    class DuplicateTabUseCase(private val store: BrowserStore) {
        /**
         * Creates a duplicate of the currently selected tab (including history) and selects it if [selectNewTab] is
         * true.
         *
         * @param selectNewTab Whether or not the duplicate tab should be selected.
         * @return The ID of the duplicated tab, or null if there is no selected tab.
         */
        fun invoke(selectNewTab: Boolean): String? {
            return store.state.selectedTab?.let {
                this.invoke(it, selectNewTab)
            }
        }

        /**
         * Creates a duplicate of [tab] (including history) and selects it if [selectNewTab] is true.
         *
         * @param tab The tab to duplicate.
         * @param selectNewTab Whether or not the duplicate tab should be selected.
         * @return The ID of the duplicated tab.
         */
        operator fun invoke(
            tab: TabSessionState,
            selectNewTab: Boolean = true,
        ): String {
            val duplicate =
                createTab(
                    url = tab.content.url,
                    private = tab.content.private,
                    contextId = tab.contextId,
                    parent = tab,
                    engineSessionState = tab.engineState.engineSessionState,
                )

            store.dispatch(
                TabListAction.AddTabAction(
                    duplicate,
                    select = selectNewTab,
                )
            )
            return duplicate.id
        }
    }

    /** Use case for moving a collection of tabs. */
    class MoveTabsUseCase(private val store: BrowserStore) {
        /**
         * Moves the tabs of [tabIds] next to [targetTabId], before/after based on [placeAfter]
         *
         * @property tabIds The IDs of the tabs to move.
         * @property targetTabId A tab that the moved tabs will be moved next to
         * @property placeAfter True if the moved tabs should be placed after the target, False for placing before the
         *   target. Irrelevant if the target is one of the tabs being moved, since then the whole list is moved to
         *   where the target was. Ordering of the moved tabs relative to each other is preserved.
         */
        operator fun invoke(
            tabIds: List<String>,
            targetTabId: String,
            placeAfter: Boolean,
        ) {
            store.dispatch(
                TabListAction.MoveTabsAction(
                    tabIds,
                    targetTabId,
                    placeAfter,
                )
            )
        }

        /**
         * Moves a [sourceTabId] next to [targetTabId] before/after based on [placeAfter]
         *
         * @param sourceTabId The ID of the tab to move
         * @param targetTabId The ID of the tab that the moved tab will be placed next to. This may be null, and will
         *   result in a no-op in that case.
         * @param placeAfter True if the tab should be placed after the target, false otherwise.
         */
        fun invoke(
            sourceTabId: String,
            targetTabId: String?,
            placeAfter: Boolean,
        ) {
            if (targetTabId != null && sourceTabId != targetTabId) {
                this.invoke(
                    listOf(sourceTabId),
                    targetTabId,
                    placeAfter,
                )
            }
        }
    }

    /**
     * Use case for reopening a private tab as a regular (ie, non-private) tab.
     *
     * To avoid complications with tab parenting etc (ie, to avoid the scenario where private tabs are parented by
     * non-private tabs) this is not a "move" operation but instead more of a "close + open" operation.
     */
    class MigratePrivateTabUseCase(private val store: BrowserStore) {
        /**
         * @param tabId the ID of the session to move.
         * @param alternativeUrl url to load. If not specified the URL from the tab will be used.
         * @return the ID of the tab that was re-created as part of the move.
         */
        operator fun invoke(
            tabId: String,
            alternativeUrl: String? = null,
        ): String {
            val tab = store.state.findTab(tabId) ?: throw IllegalStateException("Tab does not exist.")

            require(tab.content.private) { "The tab we are trying to move is not private." }

            val url = alternativeUrl ?: tab.content.url
            val newTab = createTab(url)

            store.dispatch(TabListAction.AddTabAction(newTab, true))
            store.dispatch(TabListAction.RemoveTabAction(tabId, false))

            return newTab.id
        }
    }

    val selectTab: SelectTabUseCase by lazy { DefaultSelectTabUseCase(store) }
    val removeTab: RemoveTabUseCase by lazy { DefaultRemoveTabUseCase(store) }
    val addTab: AddNewTabUseCase by lazy { AddNewTabUseCase(store) }
    val removeAllTabs: RemoveAllTabsUseCase by lazy { RemoveAllTabsUseCase(store) }
    val removeTabs: RemoveTabsUseCase by lazy { RemoveTabsUseCase(store) }
    val removeNormalTabs: RemoveNormalTabsUseCase by lazy { RemoveNormalTabsUseCase(store) }
    val removePrivateTabs: RemovePrivateTabsUseCase by lazy { RemovePrivateTabsUseCase(store) }
    val undo by lazy { UndoTabRemovalUseCase(store) }
    val restore: RestoreUseCase by lazy {
        RestoreUseCase(
            store,
            selectTab,
            mainDispatcher,
            ioDispatcher,
        )
    }
    val selectOrAddTab: SelectOrAddUseCase by lazy { SelectOrAddUseCase(store) }
    val duplicateTab: DuplicateTabUseCase by lazy { DuplicateTabUseCase(store) }
    val moveTabs: MoveTabsUseCase by lazy { MoveTabsUseCase(store) }
    val migratePrivateTabUseCase: MigratePrivateTabUseCase by lazy { MigratePrivateTabUseCase(store) }
}
