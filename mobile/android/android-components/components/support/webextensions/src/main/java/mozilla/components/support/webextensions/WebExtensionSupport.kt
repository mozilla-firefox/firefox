/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.webextensions

import androidx.annotation.VisibleForTesting
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ExtensionsProcessAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.selector.allTabs
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.concept.engine.webextension.ActionHandler
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.TabHandler
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionDelegate
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import mozilla.components.concept.engine.webextension.WebExtensionRuntime
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.isExtensionUrl
import mozilla.components.support.ktx.kotlinx.coroutines.flow.filterChanged
import mozilla.components.support.webextensions.WebExtensionSupport.initialize
import mozilla.components.support.webextensions.facts.emitWebExtensionsInitializedFact

/** Function to relay the permission request to the app / user. */
typealias onUpdatePermissionRequest =
    (
        extension: WebExtension,
        newPermissions: List<String>,
        newOrigins: List<String>,
        newDataCollectionPermissions: List<String>,
        onPermissionsGranted: ((Boolean) -> Unit),
    ) -> Unit

/**
 * Provides functionality to make sure web extension related events in the [WebExtensionRuntime] are reflected in the
 * browser state by dispatching the corresponding actions to the [BrowserStore].
 *
 * Note that this class can be removed once the browser-state migration is completed and the [WebExtensionRuntime]
 * (engine) has direct access to the [BrowserStore]: https://github.com/orgs/mozilla-mobile/projects/31
 */
object WebExtensionSupport {
    private val logger = Logger("mozac-webextensions")
    private var onUpdatePermissionRequest: onUpdatePermissionRequest? = null
    private var onExtensionsLoaded: ((List<WebExtension>) -> Unit)? = null
    private var onCloseTabOverride: ((WebExtension?, String) -> Unit)? = null
    private var onSelectTabOverride: ((WebExtension?, String) -> Unit)? = null

    val installedExtensions = ConcurrentHashMap<String, WebExtension>()

    /** A [Deferred] completed during [initialize] once the state of all installed extensions is known. */
    private val initializationResult = CompletableDeferred<Unit>()

    /** [ActionHandler] for session-specific overrides. Forwards actions to the the provided [store]. */
    private class SessionActionHandler(
        private val store: BrowserStore,
        private val sessionId: String,
    ) : ActionHandler {

        override fun onBrowserAction(extension: WebExtension, session: EngineSession?, action: Action) {
            store.dispatch(WebExtensionAction.UpdateTabBrowserAction(sessionId, extension.id, action))
        }

        override fun onPageAction(extension: WebExtension, session: EngineSession?, action: Action) {
            store.dispatch(WebExtensionAction.UpdateTabPageAction(sessionId, extension.id, action))
        }
    }

    /** [TabHandler] for session-specific tab events. Forwards actions to the the provided [store]. */
    private class SessionTabHandler(
        private val store: BrowserStore,
        private val sessionId: String,
        private val onCloseTabOverride: ((WebExtension?, String) -> Unit)? = null,
        private val onSelectTabOverride: ((WebExtension?, String) -> Unit)? = null,
    ) : TabHandler {

        override fun onCloseTab(webExtension: WebExtension, engineSession: EngineSession): Boolean {
            val tab = store.state.findTabOrCustomTab(sessionId)
            return if (tab != null) {
                closeTab(tab.id, tab.isCustomTab(), store, onCloseTabOverride, webExtension)
                true
            } else {
                false
            }
        }

        override fun onUpdateTab(
            webExtension: WebExtension,
            engineSession: EngineSession,
            active: Boolean,
            url: String?,
        ): Boolean {
            val tab = store.state.findTabOrCustomTab(sessionId)
            return if (tab != null) {
                if (active && !tab.isCustomTab()) {
                    onSelectTabOverride?.invoke(webExtension, tab.id)
                        ?: store.dispatch(TabListAction.SelectTabAction(tab.id))
                }
                url?.let {
                    engineSession.loadUrl(it)
                }
                true
            } else {
                false
            }
        }
    }

    /**
     * Registers a listener for web extension related events on the provided [WebExtensionRuntime] and reacts by
     * dispatching the corresponding actions to the provided [BrowserStore].
     *
     * @param runtime the browser [WebExtensionRuntime] to use.
     * @param store the application's [BrowserStore].
     * @param openPopupInTab (optional) flag to determine whether a browser or page action would display a web extension
     *   popup in a tab or not. Defaults to false.
     * @param isInPrivateBrowsingMode (optional) override to express the preferred private browsing mode for a new tab
     *   when it is about to open.
     * @param onNewTabOverride (optional) override of behaviour that should be triggered when web extensions open a new
     *   tab e.g. when dispatching to the store isn't sufficient while migrating from browser-session to browser-state.
     *   This is a lambda accepting the [WebExtension], the [EngineSession] to use, the URL to load, and whether the new
     *   tab should be selected/active, returning the ID of the created session.
     * @param onCloseTabOverride (optional) override of behaviour that should be triggered when web extensions close
     *   tabs e.g. when dispatching to the store isn't sufficient while migrating from browser-session to browser-state.
     *   This is a lambda accepting the [WebExtension] and the session/tab ID to close.
     * @param onSelectTabOverride (optional) override of behaviour that should be triggered when a tab is selected to
     *   display a web extension popup. This is a lambda accepting the [WebExtension] and the session/tab ID to select.
     * @param onUpdatePermissionRequest (optional) Invoked when a web extension has changed its permissions while trying
     *   to update to a new version. This requires user interaction as the updated extension will not be installed,
     *   until the user grants the new permissions.
     * @param onExtensionsLoaded (optional) callback invoked when the extensions are loaded by the engine. Note that the
     *   UI (browser/page actions etc.) may not be initialized at this point. System add-ons (built-in extensions) will
     *   not be passed along.
     */
    @Suppress("CognitiveComplexMethod")
    fun initialize(
        runtime: WebExtensionRuntime,
        store: BrowserStore,
        openPopupInTab: Boolean = false,
        mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
        isInPrivateBrowsingMode: () -> Boolean = { false },
        onNewTabOverride: ((WebExtension?, EngineSession, String, Boolean, Boolean) -> String)? = null,
        onCloseTabOverride: ((WebExtension?, String) -> Unit)? = null,
        onSelectTabOverride: ((WebExtension?, String) -> Unit)? = null,
        onUpdatePermissionRequest: onUpdatePermissionRequest? = { _, _, _, _, _ -> },
        onExtensionsLoaded: ((List<WebExtension>) -> Unit)? = null,
    ) {
        this.onUpdatePermissionRequest = onUpdatePermissionRequest
        this.onExtensionsLoaded = onExtensionsLoaded
        this.onCloseTabOverride = onCloseTabOverride
        this.onSelectTabOverride = onSelectTabOverride

        // Queries the runtime for installed extensions and adds them to the store
        registerInstalledExtensions(store, runtime, mainDispatcher)

        // Observes the store and registers action and tab handlers for newly added engine sessions
        registerHandlersForNewSessions(store, mainDispatcher)

        runtime.registerWebExtensionDelegate(
            object : WebExtensionDelegate {
                override fun isInPrivateBrowsing(): Boolean = isInPrivateBrowsingMode()

                override fun onNewTab(
                    extension: WebExtension,
                    engineSession: EngineSession,
                    active: Boolean,
                    url: String,
                    isPrivate: Boolean,
                ) {
                    openTab(
                        store,
                        onNewTabOverride,
                        onSelectTabOverride,
                        extension,
                        engineSession,
                        url,
                        active,
                        isPrivate,
                    )
                }

                override fun onBrowserActionDefined(extension: WebExtension, action: Action) {
                    store.dispatch(WebExtensionAction.UpdateBrowserAction(extension.id, action))
                }

                override fun onPageActionDefined(extension: WebExtension, action: Action) {
                    store.dispatch(WebExtensionAction.UpdatePageAction(extension.id, action))
                }

                override fun onOpenOptionsPage(extension: WebExtension) {
                    val metaData = extension.getMetadata() ?: return
                    if (metaData.openOptionsPageInTab) {
                        logger.error("The case where |open_in_tab| is true should be handled in the API script.")
                    } else {
                        val optionsPageUrl = metaData.optionsPageUrl ?: return
                        store.dispatch(
                            WebExtensionAction.UpdateOptionsPageSessionAction(
                                extensionId = extension.id,
                                optionsPageInstanceId = UUID.randomUUID().toString(),
                                optionsPageUrl = optionsPageUrl,
                                extensionTranslatedName = metaData.name ?: "",
                            )
                        )
                    }
                }

                override fun onToggleActionPopup(
                    extension: WebExtension,
                    engineSession: EngineSession,
                    action: Action,
                    isPrivate: Boolean,
                ): EngineSession? {
                    return if (!openPopupInTab) {
                        store.dispatch(WebExtensionAction.UpdatePopupSessionAction(extension.id, null, engineSession))
                        engineSession
                    } else {
                        val popupSessionId = store.state.extensions[extension.id]?.popupSessionId
                        if (popupSessionId != null && store.state.tabs.find { it.id == popupSessionId } != null) {
                            if (popupSessionId == store.state.selectedTabId) {
                                closeTab(popupSessionId, false, store, onCloseTabOverride, extension)
                            } else {
                                onSelectTabOverride?.invoke(extension, popupSessionId)
                                    ?: store.dispatch(TabListAction.SelectTabAction(popupSessionId))
                            }
                            null
                        } else {
                            val sessionId =
                                openTab(
                                    store,
                                    onNewTabOverride,
                                    onSelectTabOverride,
                                    extension,
                                    engineSession,
                                    isPrivate = isPrivate,
                                )
                            store.dispatch(WebExtensionAction.UpdatePopupSessionAction(extension.id, sessionId))
                            engineSession
                        }
                    }
                }

                override fun onInstalled(extension: WebExtension) {
                    logger.debug("onInstalled ${extension.id}")
                    // Built-in extensions are not installed by users, they are not aware of them
                    // for this reason we don't show any UI related to built-in extensions. Also,
                    // when the add-on has already been installed, we don't need to show anything
                    // either.
                    val shouldDispatchAction = !installedExtensions.containsKey(extension.id) && !extension.isBuiltIn()
                    cancelPendingExtensionUpdate(store, extension.id)
                    registerInstalledExtension(store, extension)
                    if (shouldDispatchAction) {
                        store.dispatch(
                            WebExtensionAction.UpdatePromptRequestWebExtensionAction(
                                WebExtensionPromptRequest.AfterInstallation.PostInstallation(extension)
                            )
                        )
                    }
                }

                override fun onInstallationFailedRequest(
                    extension: WebExtension?,
                    exception: WebExtensionInstallException,
                ) {
                    logger.error("onInstallationFailedRequest ${extension?.id}", exception)
                    store.dispatch(
                        WebExtensionAction.UpdatePromptRequestWebExtensionAction(
                            WebExtensionPromptRequest.BeforeInstallation.InstallationFailed(
                                extension,
                                exception,
                            )
                        )
                    )
                }

                override fun onUninstalled(extension: WebExtension) {
                    installedExtensions.remove(extension.id)
                    cancelPendingExtensionUpdate(store, extension.id)
                    store.dispatch(WebExtensionAction.UninstallWebExtensionAction(extension.id))
                }

                override fun onEnabled(extension: WebExtension) {
                    installedExtensions[extension.id] = extension
                    store.dispatch(WebExtensionAction.UpdateWebExtensionEnabledAction(extension.id, true))
                }

                override fun onOptionalPermissionsChanged(extension: WebExtension) {
                    installedExtensions[extension.id] = extension
                }

                override fun onDisabled(extension: WebExtension) {
                    installedExtensions[extension.id] = extension
                    store.dispatch(WebExtensionAction.UpdateWebExtensionEnabledAction(extension.id, false))
                }

                override fun onReady(extension: WebExtension) {
                    installedExtensions[extension.id] = extension
                }

                override fun onAllowedInPrivateBrowsingChanged(extension: WebExtension) {
                    installedExtensions[extension.id] = extension
                    store.dispatch(
                        WebExtensionAction.UpdateWebExtensionAllowedInPrivateBrowsingAction(
                            extension.id,
                            extension.isAllowedInPrivateBrowsing(),
                        )
                    )
                }

                override fun onInstallPermissionRequest(
                    extension: WebExtension,
                    permissions: List<String>,
                    origins: List<String>,
                    dataCollectionPermissions: List<String>,
                    onConfirm: (PermissionPromptResponse) -> Unit,
                ) {
                    store.dispatch(
                        WebExtensionAction.UpdatePromptRequestWebExtensionAction(
                            WebExtensionPromptRequest.AfterInstallation.Permissions.Required(
                                extension,
                                permissions,
                                origins,
                                dataCollectionPermissions,
                                onConfirm,
                            )
                        )
                    )
                }

                override fun onUpdatePermissionRequest(
                    extension: WebExtension,
                    newPermissions: List<String>,
                    newOrigins: List<String>,
                    newDataCollectionPermissions: List<String>,
                    onPermissionsGranted: ((Boolean) -> Unit),
                ) {
                    this@WebExtensionSupport.onUpdatePermissionRequest?.invoke(
                        extension,
                        newPermissions,
                        newOrigins,
                        newDataCollectionPermissions,
                        onPermissionsGranted,
                    )
                }

                override fun onOptionalPermissionsRequest(
                    extension: WebExtension,
                    permissions: List<String>,
                    origins: List<String>,
                    dataCollectionPermissions: List<String>,
                    onPermissionsGranted: ((Boolean) -> Unit),
                ) {
                    store.dispatch(
                        WebExtensionAction.UpdatePromptRequestWebExtensionAction(
                            WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                                extension,
                                permissions,
                                origins,
                                dataCollectionPermissions,
                                onPermissionsGranted,
                            )
                        )
                    )
                }

                override fun onExtensionListUpdated() {
                    installedExtensions.clear()
                    store.dispatch(WebExtensionAction.UninstallAllWebExtensionsAction)
                    registerInstalledExtensions(store, runtime, mainDispatcher)
                }

                override fun onDisabledExtensionProcessSpawning() {
                    store.dispatch(ExtensionsProcessAction.ShowPromptAction(show = true))
                }
            }
        )
    }

    /**
     * Awaits for completion of the initialization process (completes when the state of all installed extensions is
     * known).
     */
    suspend fun awaitInitialization() = initializationResult.await()

    /** Queries the [WebExtensionRuntime] for installed web extensions and adds them to the [store]. */
    private fun registerInstalledExtensions(
        store: BrowserStore,
        runtime: WebExtensionRuntime,
        mainDispatcher: CoroutineDispatcher,
    ) {
        runtime.listInstalledWebExtensions(
            onSuccess = { extensions ->
                extensions.forEach { registerInstalledExtension(store, it) }
                emitWebExtensionsInitializedFact(extensions)
                closeUnsupportedTabs(store, extensions, mainDispatcher)
                initializationResult.complete(Unit)
                onExtensionsLoaded?.invoke(extensions.filter { !it.isBuiltIn() })
            },
            onError = { throwable ->
                logger.error("Failed to query installed extension", throwable)
                initializationResult.completeExceptionally(throwable)
            },
        )
    }

    /** Marks the provided [webExtension] as installed by adding it to the [store]. */
    private fun registerInstalledExtension(store: BrowserStore, webExtension: WebExtension) {
        installedExtensions[webExtension.id] = webExtension
        store.dispatch(WebExtensionAction.InstallWebExtensionAction(webExtension.toState()))

        // Register action handler for all existing engine sessions on the new extension,
        // an issue was filed to get us an API, so we don't have to do this per extension:
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1603559
        store.state.allTabs.forEach { tab ->
            tab.engineState.engineSession?.let { session ->
                registerSessionHandlers(webExtension, store, session, tab.id)
            }
        }
    }

    /**
     * Closes any leftover extensions tabs from extensions that are no longer installed/registered. When an extension is
     * uninstalled, all extension pages will be closed. So, in theory, there should never be any leftover tabs. However,
     * since we support temporary registered extensions and also recently migrated built-in extensions from the
     * transient registerWebExtensions to the persistent installBuiltIn, we should handle this case to make sure we
     * don't have any unloadable tabs around.
     */
    private fun closeUnsupportedTabs(
        store: BrowserStore,
        extensions: List<WebExtension>,
        mainDispatcher: CoroutineDispatcher,
    ) {
        val supportedUrls = extensions.mapNotNull { it.getMetadata()?.baseUrl }

        // We only need to do this a single time, once tabs are restored. We need to observe the
        // store (instead of querying it directly), as tabs can be restored asynchronously on
        // startup and might not be ready yet.
        var scope: CoroutineScope? = null
        scope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .map { state -> state.tabs.filter { it.restored }.size }
                    .distinctUntilChanged()
                    .collect { size ->
                        if (size > 0) {
                            store.state.tabs.forEach { tab ->
                                val tabUrl = tab.content.url
                                if (tabUrl.isExtensionUrl() && supportedUrls.none { tabUrl.startsWith(it) }) {
                                    closeTab(tab.id, false, store, onCloseTabOverride)
                                }
                            }
                            scope?.cancel()
                        }
                    }
            }
    }

    /** Marks the provided [updatedExtension] as updated in the [store]. */
    fun markExtensionAsUpdated(store: BrowserStore, updatedExtension: WebExtension) {
        installedExtensions[updatedExtension.id] = updatedExtension
        cancelPendingExtensionUpdate(store, updatedExtension.id)
        store.dispatch(WebExtensionAction.UpdateWebExtensionAction(updatedExtension.toState()))

        // Register action handler for all existing engine sessions on the new extension
        store.state.allTabs.forEach { tab ->
            tab.engineState.engineSession?.let { session ->
                registerSessionHandlers(updatedExtension, store, session, tab.id)
            }
        }
    }

    private fun cancelPendingExtensionUpdate(store: BrowserStore, extensionId: String) {
        val pendingUpdate = store.state.pendingExtensionUpdates[extensionId] ?: return
        store.dispatch(WebExtensionAction.RemovePendingExtensionUpdateAction(extensionId))
        pendingUpdate.complete(granted = false)
    }

    /**
     * Observes the provided store to register session-specific [ActionHandler]s for all installed extensions on newly
     * added sessions.
     */
    private fun registerHandlersForNewSessions(store: BrowserStore, mainDispatcher: CoroutineDispatcher) {
        // We need to observe for the entire lifetime of the application,
        // as web extension support is not tied to any particular view.
        store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow
                .mapNotNull { state -> state.allTabs }
                .filterChanged {
                    it.engineState.engineSession
                }
                .collect { state ->
                    state.engineState.engineSession?.let { session ->
                        installedExtensions.values.forEach { extension ->
                            registerSessionHandlers(extension, store, session, state.id)
                        }
                    }
                }
        }
    }

    private fun registerSessionHandlers(
        extension: WebExtension,
        store: BrowserStore,
        session: EngineSession,
        sessionId: String,
    ) {
        if (extension.supportActions && !extension.hasActionHandler(session)) {
            val actionHandler = SessionActionHandler(store, sessionId)
            extension.registerActionHandler(session, actionHandler)
        }

        if (!extension.hasTabHandler(session)) {
            val tabHandler = SessionTabHandler(store, sessionId, onCloseTabOverride, onSelectTabOverride)
            extension.registerTabHandler(session, tabHandler)
        }
    }

    private fun openTab(
        store: BrowserStore,
        onNewTabOverride: ((WebExtension?, EngineSession, String, Boolean, Boolean) -> String)? = null,
        onSelectTabOverride: ((WebExtension?, String) -> Unit)? = null,
        webExtension: WebExtension?,
        engineSession: EngineSession,
        url: String = "",
        selected: Boolean = true,
        isPrivate: Boolean = false,
    ): String {
        return if (onNewTabOverride != null) {
            val sessionId = onNewTabOverride.invoke(webExtension, engineSession, url, selected, isPrivate)
            if (selected) {
                onSelectTabOverride?.invoke(webExtension, sessionId)
            }
            sessionId
        } else {
            val tab = createTab(url, private = isPrivate)
            store.dispatch(TabListAction.AddTabAction(tab, selected))
            store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession))
            tab.id
        }
    }

    private fun closeTab(
        id: String,
        customTab: Boolean,
        store: BrowserStore,
        onCloseTabOverride: ((WebExtension?, String) -> Unit)? = null,
        webExtension: WebExtension? = null,
    ) {
        if (onCloseTabOverride != null) {
            onCloseTabOverride.invoke(webExtension, id)
        } else {
            val action =
                if (customTab) {
                    CustomTabListAction.RemoveCustomTabAction(id)
                } else {
                    TabListAction.RemoveTabAction(id)
                }

            store.dispatch(action)
        }
    }

    @VisibleForTesting
    internal fun WebExtension.toState() =
        WebExtensionState(
            id,
            url,
            getMetadata()?.name,
            isEnabled(),
            isAllowedInPrivateBrowsing(),
            isBuiltIn(),
        )

    private fun SessionState.isCustomTab() = this is CustomTabSessionState
}
